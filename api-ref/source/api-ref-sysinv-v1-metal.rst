====================================================
Bare Metal API v1
====================================================

Manage physical servers with the StarlingX Bare Metal API. This
includes inventory collection and configuration of nodes, ports,
CPUs, disks, partitions, memory, and sensors.

The typical port used for the Bare Metal REST API is 6385. However, proper
technique would be to look up the sysinv service endpoint in Keystone.

-------------
API versions
-------------

**************************************************************************
Lists information about all StarlingX Bare Metal API versions
**************************************************************************

.. rest_method:: GET /

**Normal response codes**

200, 300

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

::

   {
      "default_version":{
         "id":"v1",
         "links":[
            {
               "href":"http://128.224.150.54:6385/v1/",
               "rel":"self"
            }
         ]
      },
      "versions":[
         {
            "id":"v1",
            "links":[
               {
                  "href":"http://128.224.150.54:6385/v1/",
                  "rel":"self"
               }
            ]
         }
      ],
      "description":"StarlingX System API allows for the management of physical servers.  This includes inventory collection and configuration of hosts, ports, interfaces, CPUs, disk, memory, and system configuration.  The API also supports the configuration of the cloud's SNMP interface. ",
      "name":"StarlingX SysInv API"
   }

This operation does not accept a request body.

*******************************************
Shows details for Bare Metal API v1
*******************************************

.. rest_method:: GET /v1

**Normal response codes**

200, 203

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

::

   {
       "lldp_neighbours": [
           {
               "href": "http://10.10.10.2:6385/v1/lldp_neighbours/",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/lldp_neighbours/",
               "rel": "bookmark"
           }
       ],
       "ihosts": [
           {
               "href": "http://10.10.10.2:6385/v1/ihosts/",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/ihosts/",
               "rel": "bookmark"
           }
       ],
       "icpu": [
           {
               "href": "http://10.10.10.2:6385/v1/icpu/",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/icpu/",
               "rel": "bookmark"
           }
       ],
       "lldp_agents": [
           {
               "href": "http://10.10.10.2:6385/v1/lldp_agents/",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/lldp_agents/",
               "rel": "bookmark"
           }
       ],
       "iport": [
           {
               "href": "http://10.10.10.2:6385/v1/iport/",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/iport/",
               "rel": "bookmark"
           }
       ],
   }

This operation does not accept a request body.

------
Hosts
------

Hosts are the physical hosts or servers for the system.

*************************
Lists all host entities
*************************

.. rest_method:: GET /v1/ihosts

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "ihosts (Optional)", "plain", "xsd:list", "The list of host entities."
   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."

::

   {
      "ihosts":[
         {
            "reserved":"False",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984",
                  "rel":"bookmark"
               }
            ],
            "bm_ip":"",
            "updated_at":"2014-10-02T14:56:23.230316+00:00",
            "bm_username":null,
            "iprofile_uuid":null,
            "id":1,
            "uptime":68379,
            "mgmt_ip":"192.168.204.3",
            "hostname":"controller-0",
            "capabilities":{
               "stor_function":"monitor",
               "Personality":"Controller-Active"
            },
            "operational":"enabled",
            "availability":"available",
            "location":{

            },
            "config_applied":"18c9e850-be49-4b84-9eba-6aaeab12ec72",
            "administrative":"unlocked",
            "personality":"controller",
            "config_status":"Config out-of-date",
            "config_target":"a47cfb0d-3892-4608-8012-371ce45faf55",
            "mgmt_mac":"08:00:27:3d:c2:fe",
            "task":"",
            "created_at":"2014-10-01T20:06:44.302456+00:00",
            "uuid":"298d0050-7758-4bb8-aefc-dfddad2c4984",
            "action":"none",
            "bm_type":null
         },
         {
            "reserved":"False",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/ihosts/5f7d15c6-77aa-49cd-a6a1-678aef89edea",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/ihosts/5f7d15c6-77aa-49cd-a6a1-678aef89edea",
                  "rel":"bookmark"
               }
            ],
            "bm_ip":"",
            "updated_at":"2014-10-02T14:56:23.252439+00:00",
            "bm_username":"",
            "iprofile_uuid":null,
            "id":2,
            "uptime":65518,
            "mgmt_ip":"192.168.204.4",
            "hostname":"controller-1",
            "capabilities":{
               "stor_function":"monitor",
               "Personality":"Controller-Standby"
            },
            "operational":"enabled",
            "availability":"available",
            "location":{
               "locn":""
            },
            "config_applied":"18c9e850-be49-4b84-9eba-6aaeab12ec72",
            "administrative":"unlocked",
            "personality":"controller",
            "config_status":"Config out-of-date",
            "config_target":"a47cfb0d-3892-4608-8012-371ce45faf55",
            "mgmt_mac":"08:00:27:90:be:dc",
            "task":"",
            "created_at":"2014-10-01T20:07:11.401964+00:00",
            "uuid":"5f7d15c6-77aa-49cd-a6a1-678aef89edea",
            "action":"none",
            "bm_type":null
         },
         {
            "reserved":"False",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923",
                  "rel":"bookmark"
               }
            ],
            "bm_ip":"",
            "updated_at":"2014-10-02T15:00:23.445512+00:00",
            "bm_username":"",
            "iprofile_uuid":null,
            "id":5,
            "uptime":63720,
            "mgmt_ip":"192.168.204.5",
            "hostname":"storage-0",
            "capabilities":{
               "stor_function":"monitor"
            },
            "operational":"disabled",
            "availability":"online",
            "location":{
               "locn":""
            },
            "config_applied":null,
            "administrative":"locked",
            "personality":"storage",
            "config_status":null,
            "config_target":null,
            "mgmt_mac":"08:00:27:fa:e2:1c",
            "task":"",
            "created_at":"2014-10-01T21:12:09.899675+00:00",
            "uuid":"0dad0322-f289-40ca-9059-67cd673a0923",
            "action":"none",
            "bm_type":null
         },
         {
            "reserved":"False",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/ihosts/42d72247-e0e3-4a5a-8cb1-40bbee52c8db",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/ihosts/42d72247-e0e3-4a5a-8cb1-40bbee52c8db",
                  "rel":"bookmark"
               }
            ],
            "bm_ip":"",
            "updated_at":"2014-10-02T14:56:23.268242+00:00",
            "bm_username":"",
            "iprofile_uuid":null,
            "id":6,
            "uptime":62651,
            "mgmt_ip":"192.168.204.6",
            "hostname":"storage-1",
            "capabilities":{

            },
            "operational":"disabled",
            "availability":"online",
            "location":{
               "locn":""
            },
            "config_applied":null,
            "administrative":"locked",
            "personality":"storage",
            "config_status":null,
            "config_target":null,
            "mgmt_mac":"08:00:27:22:48:f2",
            "task":"",
            "created_at":"2014-10-01T21:26:17.404218+00:00",
            "uuid":"42d72247-e0e3-4a5a-8cb1-40bbee52c8db",
            "action":"none",
            "bm_type":null
         },
         {
            "reserved":"False",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/ihosts/cd5ef327-618b-4aac-9b10-9bbbe2baa8e0",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/ihosts/cd5ef327-618b-4aac-9b10-9bbbe2baa8e0",
                  "rel":"bookmark"
               }
            ],
            "bm_ip":null,
            "updated_at":null,
            "bm_username":null,
            "iprofile_uuid":null,
            "id":7,
            "uptime":0,
            "mgmt_ip":"192.168.204.129",
            "hostname":null,
            "capabilities":{

            },
            "operational":"disabled",
            "availability":"offline",
            "location":{

            },
            "config_applied":null,
            "administrative":"locked",
            "personality":null,
            "config_status":null,
            "config_target":null,
            "mgmt_mac":"08:00:27:be:6e:25",
            "task":null,
            "created_at":"2014-10-02T13:57:04.900900+00:00",
            "uuid":"cd5ef327-618b-4aac-9b10-9bbbe2baa8e0",
            "action":"none",
            "bm_type":null
         }
      ]
   }

This operation does not accept a request body.

**************************************************
Shows detailed information about a specific host
**************************************************

.. rest_method:: GET /v1/ihosts/​{host_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "invProvisioned (Optional)", "plain", "xsd:string", "Indicates whether the host has the minimum level of provisioning or not. Only a ``provisioned`` host can be unlocked."
   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."

::

   {
      "ports":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/ports",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/ports",
            "rel":"bookmark"
         }
      ],
      "reserved":"False",
      "idisks":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/idisks",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/idisks",
            "rel":"bookmark"
         }
      ],
      "bm_ip":"",
      "updated_at":"2014-10-02T15:14:23.744473+00:00",
      "bm_username":null,
      "iprofile_uuid":null,
      "id":1,
      "forisystemid":1,
      "icpus":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/icpus",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/icpus",
            "rel":"bookmark"
         }
      ],
      "uptime":69459,
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984",
            "rel":"bookmark"
         }
      ],
      "mgmt_ip":"192.168.204.3",
      "hostname":"controller-0",
      "istors":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/istors",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/istors",
            "rel":"bookmark"
         }
      ],
      "capabilities":{
         "stor_function":"monitor",
         "Personality":"Controller-Active"
      },
      "availability":"available",
      "location":{

      },
      "config_applied":"18c9e850-be49-4b84-9eba-6aaeab12ec72",
      "invprovision":"provisioned",
      "administrative":"unlocked",
      "personality":"controller",
      "iinterfaces":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/iinterfaces",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/iinterfaces",
            "rel":"bookmark"
         }
      ],
      "config_status":"Config out-of-date",
      "config_target":"a47cfb0d-3892-4608-8012-371ce45faf55",
      "isystem_uuid":"e79e74a5-e08e-41ab-9277-5e01457a0e5e",
      "mgmt_mac":"08:00:27:3d:c2:fe",
      "task":"",
      "recordtype":"standard",
      "operational":"enabled",
      "created_at":"2014-10-01T20:06:44.302456+00:00",
      "uuid":"298d0050-7758-4bb8-aefc-dfddad2c4984",
      "action":"none",
      "install_state": "installed",
      "install_state_info": "",
      "bm_type":null,
      "serialId":null,
      "boot_device": "sda",
      "rootfs_device": "sda",
      "install_output": "text",
      "console": "ttyS0,115200",
      "inodes":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/inodes",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/inodes",
            "rel":"bookmark"
         }
      ],
      "imemorys":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/imemorys",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/imemorys",
            "rel":"bookmark"
         }
      ]
   }

This operation does not accept a request body.

**************************
Modifies a specific host
**************************

.. rest_method:: PATCH /v1/ihosts/​{host_id}​

The atrributes of a Host which are modifiable:

-  personality,

-  hostname,

-  bm_type,

-  bm_ip,

-  bm_username,

-  bm_password,

-  serialid,

-  location,

-  boot_device,

-  rootfs_device,

-  install_output,

-  console,

-  ttys_dcd.

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."
   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are 'text' or 'graphical'. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: 'ttyS0,115200'."
   "ttys_dcd (Optional)", "plain", "xsd:string", "This attribute specifies whether serial port data carrier detect is enabled."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."

::

   [
      {
         "path":"/location",
         "value":"{'locn':'350 Terry Fox Dr, Kanata, Ontario, Canada'}",
         "op":"replace"
      }
   ]

::

   {
      "ports":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/ports",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/ports",
            "rel":"bookmark"
         }
      ],
      "reserved":"False",
      "idisks":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/idisks",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/idisks",
            "rel":"bookmark"
         }
      ],
      "bm_ip":"",
      "updated_at":"2014-10-02T15:19:42.572251+00:00",
      "bm_username":null,
      "iprofile_uuid":null,
      "id":1,
      "forisystemid":1,
      "icpus":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/icpus",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/icpus",
            "rel":"bookmark"
         }
      ],
      "uptime":69459,
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984",
            "rel":"bookmark"
         }
      ],
      "mgmt_ip":"192.168.204.3",
      "hostname":"controller-0",
      "istors":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/istors",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/istors",
            "rel":"bookmark"
         }
      ],
      "capabilities":{
         "stor_function":"monitor"
      },
      "availability":"available",
      "location":{
         "locn":"350 Terry Fox Dr, Kanata, Ontario, Canada"
      },
      "config_applied":"18c9e850-be49-4b84-9eba-6aaeab12ec72",
      "invprovision":"provisioned",
      "administrative":"unlocked",
      "personality":"controller",
      "iinterfaces":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/iinterfaces",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/iinterfaces",
            "rel":"bookmark"
         }
      ],
      "config_status":"Config out-of-date",
      "config_target":"a47cfb0d-3892-4608-8012-371ce45faf55",
      "isystem_uuid":"e79e74a5-e08e-41ab-9277-5e01457a0e5e",
      "mgmt_mac":"08:00:27:3d:c2:fe",
      "task":null,
      "ttys_dcd":null,
      "recordtype":"standard",
      "operational":"enabled",
      "created_at":"2014-10-01T20:06:44.302456+00:00",
      "uuid":"298d0050-7758-4bb8-aefc-dfddad2c4984",
      "action":"none",
      "bm_type":null,
      "serialId":null,
      "boot_device": "sda",
      "rootfs_device": "sda",
      "install_output": "text",
      "console": "ttyS0,115200",
      "inodes":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/inodes",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/inodes",
            "rel":"bookmark"
         }
      ],
      "imemorys":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/imemorys",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/298d0050-7758-4bb8-aefc-dfddad2c4984/imemorys",
            "rel":"bookmark"
         }
      ]
   }

***************************************
Executes an action on a specific host
***************************************

.. rest_method:: PATCH /v1/ihosts/​{host_id}​

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."
   "action (Optional)", "plain", "xsd:string", "Perform one of the following actions to the host: Valid values are: ``unlock``, ``lock``, ``swact``, ``apply-profile``, ``reboot``, ``reset``, ``power-on``, ``power-off``, or ``reinstall``."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."

::

   [
      {
         "path":"/action",
         "value":"unlock",
         "op":"replace"
      }
   ]

::

   {
      "ports":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/ports",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/ports",
            "rel":"bookmark"
         }
      ],
      "reserved":"False",
      "idisks":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/idisks",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/idisks",
            "rel":"bookmark"
         }
      ],
      "bm_ip":"",
      "updated_at":"2014-10-02T15:31:31.565491+00:00",
      "bm_username":"",
      "iprofile_uuid":null,
      "id":5,
      "forisystemid":1,
      "icpus":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/icpus",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/icpus",
            "rel":"bookmark"
         }
      ],
      "uptime":107,
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923",
            "rel":"bookmark"
         }
      ],
      "mgmt_ip":"192.168.204.5",
      "hostname":"storage-0",
      "istors":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/istors",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/istors",
            "rel":"bookmark"
         }
      ],
      "capabilities":{
         "stor_function":"monitor"
      },
      "availability":"online",
      "location":{
         "locn":""
      },
      "config_applied":"a47cfb0d-3892-4608-8012-371ce45faf55",
      "invprovision":"provisioned",
      "administrative":"locked",
      "personality":"storage",
      "iinterfaces":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/iinterfaces",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/iinterfaces",
            "rel":"bookmark"
         }
      ],
      "config_status":"",
      "config_target":null,
      "isystem_uuid":"e79e74a5-e08e-41ab-9277-5e01457a0e5e",
      "mgmt_mac":"08:00:27:fa:e2:1c",
      "task":"Unlocking",
      "recordtype":"standard",
      "operational":"disabled",
      "created_at":"2014-10-01T21:12:09.899675+00:00",
      "uuid":"0dad0322-f289-40ca-9059-67cd673a0923",
      "action":"none",
      "bm_type":null,
      "serialId":null,
      "boot_device": "sda",
      "rootfs_device": "sda",
      "install_output": "text",
      "console": "ttyS0,115200",
      "inodes":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/inodes",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/inodes",
            "rel":"bookmark"
         }
      ],
      "imemorys":[
         {
            "href":"http://192.168.204.2:6385/v1/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/imemorys",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/ihosts/0dad0322-f289-40ca-9059-67cd673a0923/imemorys",
            "rel":"bookmark"
         }
      ]
   }

*************************
Deletes a specific host
*************************

.. rest_method:: DELETE /v1/ihosts/​{host_id}​

**Normal response codes**

204

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

This operation does not accept a request body.

****************
Creates a host
****************

.. rest_method:: POST /v1/ihosts

Note that a host should only be added through the REST API if the system
is not already configured to be automatically added by the system. This
is determined by configuration option during config_controller at system
installation.

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "hostname", "plain", "xsd:string", "The hostname for the host. Must be a unique name."
   "personality", "plain", "xsd:string", "The role of of this host: i.e. ``controller``, ``storage``, ``compute`` ."
   "mgmt_mac", "plain", "xsd:string", "The MAC address of the host's management interface. Must be unique."
   "mgmt_ip", "plain", "xsd:string", "The IP address of the host's management interface. Must be unique."
   "bm_type (Optional)", "plain", "xsd:string", "This attribute specifies whether board management controller type is ``bmc``. ``bmc`` enables Board Management Controller. Default is None to indicate no board management controller. If bm_type is specified, then bm_ip, bm_username, and bm_password are also required."
   "bm_ip (Optional)", "plain", "xsd:string", "Only applicable if ``bm_type`` is not None. This attribute specifies the host's board management controller interface IP address. ``bm_ip`` is not allowed to be added if the system is configured with board management (e.g. board management subnet and vlan) at installation (config_controller)."
   "bm_username (Optional)", "plain", "xsd:string", "Only applicable if ``bm_type`` is not None. This attribute specifies the host's board management controller username."
   "bm_password (Optional)", "plain", "xsd:string", "Only applicable if ``bm_type`` is not None. This attribute specifies the host's board management controller password."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are 'text' or 'graphical'. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: 'ttyS0,115200'."
   "ttys_dcd (Optional)", "plain", "xsd:string", "This attribute specifies whether serial port data carrier detect is enabled."
   "location (Optional)", "plain", "xsd:dict", "The location of the host. Must be a dictinoary with a single parameter 'locn'."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."
   "ports (Optional)", "plain", "xsd:string", "Link to the ports resources on the host."
   "iinterfaces (Optional)", "plain", "xsd:string", "Link to the network interfaces resources on the host."
   "ethernet_ports (Optional)", "plain", "xsd:string", "Link to the ethernet ports resources on the host."
   "inodes (Optional)", "plain", "xsd:string", "Link to the numa node resources on the host."
   "imemorys (Optional)", "plain", "xsd:string", "Link to the memory resources on the host."
   "idisks (Optional)", "plain", "xsd:string", "Link to the disks resources on the host."
   "istors (Optional)", "plain", "xsd:string", "Link to the storage resources on the host."
   "ipvs (Optional)", "plain", "xsd:string", "Link to the physical volume storage resources on the host."
   "ilvgs (Optional)", "plain", "xsd:string", "Link to the logical volume group storage resources on the host."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "hostname":"compute-0",
      "personality":"compute",
      "subfunctions":"compute_lowlatency",
      "mgmt_mac":"11:22:33:44:55:66",
      "mgmt_ip":"192.168.204.200",
      "bm_type":"bmc",
      "bm_ip":"10.10.10.240",
      "bm_username":"bm_user",
      "bm_password":"bm_user_pwd",
      "boot_device": "sda",
      "rootfs_device": "sda",
      "install_output": "text",
      "console": "ttyS0,115200",
      "ttys_dcd":"True",
      "location":{"locn":"West tower, Room B"}
   }

::

   {
      "ports":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ports",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ports",
            "rel":"bookmark"
         }
      ],
      "reserved":"False",
      "idisks":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/idisks",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/idisks",
            "rel":"bookmark"
         }
      ],
      "subfunctions":"compute_lowlatency",
      "bm_ip":"10.10.10.240",
      "updated_at":null,
      "ihost_action":null,
      "bm_username":"bm_user",
      "id":3,
      "serialid":null,
      "availability":"offline",
      "forisystemid":1,
      "vim_progress_status":null,
      "icpus":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/icpus",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/icpus",
            "rel":"bookmark"
         }
      ],
      "uptime":0,
      "links":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755",
            "rel":"bookmark"
         }
      ],
      "mgmt_ip":"192.168.204.200",
      "hostname":"compute-0",
      "istors":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/istors",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/istors",
            "rel":"bookmark"
         }
      ],
      "capabilities":{

      },
      "iprofile_uuid":null,
      "location":{

      },
      "config_applied":null,
      "invprovision":null,
      "mgmt_mac":   "11:22:33:44:55:66", "administrative":"locked",
      "personality":"compute",
      "iinterfaces":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/iinterfaces",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/iinterfaces",
            "rel":"bookmark"
         }
      ],
      "isystem_uuid":"b3bbc885-2389-43e8-8b00-54a3ad6614af",
      "config_target":null,
      "ethernet_ports":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ethernet_ports",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ethernet_ports",
            "rel":"bookmark"
         }
      ],
      "uuid":"88d437b5-aa2c-4f1b-8f27-d13330dca755",
      "subfunction_oper":"disabled",
      "task":null,
      "ttys_dcd":null,
      "recordtype":"standard",
      "operational":"disabled",
      "created_at":   "2015-05-06T17:06:13.506319+00:00", "subfunction_avail":"offline",
      "ipvs":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ipvs",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ipvs",
            "rel":"bookmark"
         }
      ],
      "ilvgs":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ilvgs",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ilvgs",
            "rel":"bookmark"
         }
      ],
      "action":"none",
      "bm_type":"bmc",
      "boot_device": "sda",
      "rootfs_device": "sda",
      "install_output": "text",
      "console": "ttyS0,115200",
      "ports":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ports",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/ports",
            "rel":"bookmark"
         }
      ],
      "inodes":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/inodes",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/inodes",
            "rel":"bookmark"
         }
      ],
      "imemorys":[
         {
            "href":         "http://192.168.204.2:6385/v1/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/imemorys",
            "rel":"self"
         },
         {
            "href":         "http://192.168.204.2:6385/ihosts/88d437b5-aa2c-4f1b-8f27-d13330dca755/imemorys",
            "rel":"bookmark"
         }
      ]
   }

****************************************
Creates multiple hosts from a template
****************************************

.. rest_method:: POST /v1/ihosts/bulk_add

Accepts an XML file containing the specifications of hosts to be added
to the system and performs a host-add for each. Refer to the
Administration Guide for XML specifications.

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413)

::

   {
      "success":"compute-0, compute-1"
      "error":"compute-2: Host-add Rejected: Host with mgmt_mac 08:00:28:A9:54:19 already exists"
   }

******************************************************
Export hosts definition file from an existing system
******************************************************

.. rest_method:: GET /v1/ihosts/bulk_export

Output XML string is well formatted (with line breaks and indent)

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413)

::

   {"content": "<?xml version=\"1.0\" ?>\n<hosts>\n\t<host>\n\t\t<personality>controller</personality>\n\t\t<mgmt_mac>08:00:27:d0:e0:2b</mgmt_mac>\n\t\t<mgmt_ip>192.168.204.3</mgmt_ip>\n\t\t<!--Uncomment the statement below to power on the host automatically through board management.-->\n\t\t<!--<power_on />-->\n\t\t<bm_type/>\n\t\t<bm_username/>\n\t\t<bm_password/>\n\t\t<boot_device>sda</boot_device>\n\t\t<rootfs_device>sda</rootfs_device>\n\t\t<install_output>text</install_output>\n\t\t<console>ttyS0,115200</console>\n\t</host>\n\t<host>\n\t\t<personality>compute</personality>\n\t\t<mgmt_mac>08:00:27:bf:29:39</mgmt_mac>\n\t\t<mgmt_ip>192.168.204.20</mgmt_ip>\n\t\t<location/>\n\t\t<!--Uncomment the statement below to power on the host automatically through board management.-->\n\t\t<!--<power_on />-->\n\t\t<bm_type/>\n\t\t<bm_username/>\n\t\t<bm_password/>\n\t\t<boot_device>sda</boot_device>\n\t\t<rootfs_device>sda</rootfs_device>\n\t\t<install_output>text</install_output>\n\t\t<console>ttyS0,115200</console>\n\t</host>\n</hosts>\n"}

This operation does not accept a request body.

*****************
Upgrades a host
*****************

.. rest_method:: POST /v1/ihosts/​{host_id}​/upgrade

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."
   "force (Optional)", "plain", "xsd:boolean", "Set to true to perform the action even if the host is offline."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."

::

   {"force": false}

::

   {
     "iports": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/iports",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/iports",
         "rel": "bookmark"
       }
     ],
     "reserved": "False",
     "links": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95",
         "rel": "bookmark"
       }
     ],
     "idisks": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/idisks",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/idisks",
         "rel": "bookmark"
       }
     ],
     "subfunctions": "compute",
     "config_applied": "install",
     "bm_ip": "",
     "updated_at": "2017-03-06T16:02:47.042128+00:00",
     "isensors": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/isensors",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/isensors",
         "rel": "bookmark"
       }
     ],
     "ceph_mon": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ceph_mon",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ceph_mon",
         "rel": "bookmark"
       }
     ],
     "ihost_action": "lock",
     "bm_username": "",
     "id": 3,
     "iprofile_uuid": null,
     "serialid": null,
     "availability": "online",
     "forisystemid": 1,
     "vim_progress_status": "services-disabled",
     "icpus": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/icpus",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/icpus",
         "rel": "bookmark"
       }
     ],
     "uptime": 1112,
     "console": "",
     "uuid": "bed0aee2-d637-488e-ada1-c837ee503f95",
     "mgmt_ip": "192.168.204.247",
     "software_load": "15.12",
     "config_status": null,
     "hostname": "compute-0",
     "istors": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/istors",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/istors",
         "rel": "bookmark"
       }
     ],
     "capabilities": {},
     "operational": "disabled",
     "location": {
       "locn": ""
     },
     "invprovision": "provisioned",
     "administrative": "locked",
     "personality": "compute",
     "iinterfaces": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/iinterfaces",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/iinterfaces",
         "rel": "bookmark"
       }
     ],
     "pci_devices": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/pci_devices",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/pci_devices",
         "rel": "bookmark"
       }
     ],
     "ethernet_ports": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ethernet_ports",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ethernet_ports",
         "rel": "bookmark"
       }
     ],
     "mtce_info": null,
     "isensorgroups": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/isensorgroups",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/isensorgroups",
         "rel": "bookmark"
       }
     ],
     "isystem_uuid": "4d31d98d-4992-445a-b749-485ce6077fd2",
     "boot_device": "sda",
     "rootfs_device": "sda",
     "mgmt_mac": "08:00:27:f2:60:5a",
     "subfunction_oper": "disabled",
     "peers": null,
     "task": "",
     "ttys_dcd": "False",
     "target_load": "16.10",
     "lldp_neighbours": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/lldp_neighbours",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/lldp_neighbours",
         "rel": "bookmark"
       }
     ],
     "created_at": "2016-11-28T17:40:21.476162+00:00",
     "subfunction_avail": "online",
     "install_output": "graphical",
     "ipvs": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ipvs",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ipvs",
         "rel": "bookmark"
       }
     ],
     "ilvgs": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ilvgs",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ilvgs",
         "rel": "bookmark"
       }
     ],
     "action": "none",
     "bm_type": "",
     "lldp_agents": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/lldp_agents",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/lldp_agents",
         "rel": "bookmark"
       }
     ],
     "imemorys": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/imemorys",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/imemorys",
         "rel": "bookmark"
       }
     ],
     "ports": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ports",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/ports",
         "rel": "bookmark"
       }
     ],
     "inodes": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/inodes",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/bed0aee2-d637-488e-ada1-c837ee503f95/inodes",
         "rel": "bookmark"
       }
     ],
     "config_target": null
   }

*******************
Downgrades a host
*******************

.. rest_method:: POST /v1/ihosts/​{host_id}​/downgrade

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."
   "force (Optional)", "plain", "xsd:boolean", "Set to true to perform the action even if the host is offline."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "hostname (Optional)", "plain", "xsd:string", "The name provisioned for the host."
   "personality (Optional)", "plain", "xsd:string", "The role of the host: ``controller``, ``compute`` or ``storage``."
   "administrative (Optional)", "plain", "xsd:string", "The administrative state of the host; ``unlocked`` or ``locked``."
   "operational (Optional)", "plain", "xsd:string", "The operational state of the host; ``enabled`` or ``disabled``."
   "availability (Optional)", "plain", "xsd:string", "The availability state of the host; ``offline``, ``online``, ``intest``, ``available``, ``degraded`` or ``failed``."
   "mgmt_mac (Optional)", "plain", "xsd:string", "The management MAC of the host management interface."
   "mgmt_ip (Optional)", "plain", "xsd:string", "The management IP Address of the host."
   "task (Optional)", "plain", "xsd:string", "The current maintenance task in progress on the host."
   "serialid (Optional)", "plain", "xsd:string", "The serial id configured for the host."
   "bm_type (Optional)", "plain", "xsd:string", "The board management type of the host."
   "bm_username (Optional)", "plain", "xsd:string", "The board management username of the host."
   "bm_ip (Optional)", "plain", "xsd:string", "The board management IP Address of the host."
   "boot_device", "plain", "xsd:string", "Device used for boot partition, relative to /dev. Default: sda"
   "rootfs_device", "plain", "xsd:string", "Device used for rootfs and platform partitions, relative to /dev. Default: sda"
   "install_output", "plain", "xsd:string", "Installation output format. Values are text or graphical. Default: text"
   "console", "plain", "xsd:string", "Serial console configuration, specifying port and baud rate. Default: ttyS0,115200."
   "config_applied (Optional)", "plain", "csapi:UUID", "The configuration UUID applied to the host."
   "config_target (Optional)", "plain", "csapi:UUID", "The configuration target UUID of the host."
   "config_status (Optional)", "plain", "xsd:string", "The configuration status of the host."
   "uptime (Optional)", "plain", "xsd:string", "The uptime in seconds of the host."
   "location (Optional)", "plain", "xsd:string", "The location information of the host."
   "subfunctions (Optional)", "plain", "xsd:string", "The list of roles supported by the host. Comma separated string. For a host with compute role, the compute subfunction is configurable on initial installation, and may be either: ``compute`` or ``compute_lowlatency``."
   "subfunction_oper (Optional)", "plain", "xsd:string", "The subfunction operational state, excluding the primary role personality."
   "subfunction_avail (Optional)", "plain", "xsd:string", "The subfunction availability state, excluding the primary role personality."
   "recordtype (Optional)", "plain", "xsd:string", "The recordtype of the host: ``standard`` or ``profile``."
   "id (Optional)", "plain", "xsd:string", "Id value of the host."
   "ihost_action (Optional)", "plain", "xsd:string", "Action on the host in progress."
   "install_state (Optional)", "plain", "xsd:string", "The installation state of the host; ``preinstall``, ``installing``, ``postinstall``, ``installed`` or ``failed``."
   "install_state_status (Optional)", "plain", "xsd:string", "Progress information of the installation of the host. For example, installing 2/1040, indicating the number of packages installed out of the total packages to be installed."
   "vim_progress_status (Optional)", "plain", "xsd:string", "virtual infrastructure manager progress status."
   "ttys_dcd (Optional)", "plain", "xsd:string", "Serial port data carrier detect status."
   "software_load (Optional)", "plain", "xsd:string", "The version of the software currently running on the host."
   "target_load (Optional)", "plain", "xsd:string", "The version of the software requested to run on the host."

::

   {"force": false}

::

   {
     "iports": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/iports",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/iports",
         "rel": "bookmark"
       }
     ],
     "reserved": "False",
     "links": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade",
         "rel": "bookmark"
       }
     ],
     "idisks": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/idisks",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/idisks",
         "rel": "bookmark"
       }
     ],
     "subfunctions": "compute",
     "config_applied": "install",
     "bm_ip": "",
     "updated_at": "2017-03-06T16:16:10.126508+00:00",
     "isensors": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/isensors",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/isensors",
         "rel": "bookmark"
       }
     ],
     "ceph_mon": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ceph_mon",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ceph_mon",
         "rel": "bookmark"
       }
     ],
     "ihost_action": "lock",
     "bm_username": "",
     "id": 4,
     "iprofile_uuid": null,
     "serialid": null,
     "availability": "offline",
     "forisystemid": 1,
     "vim_progress_status": "services-disabled",
     "icpus": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/icpus",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/icpus",
         "rel": "bookmark"
       }
     ],
     "uptime": 0,
     "console": "",
     "uuid": "e6c1a877-a332-46dd-821d-e5fa9e2c4ade",
     "mgmt_ip": "192.168.204.80",
     "software_load": "15.12",
     "config_status": null,
     "hostname": "compute-1",
     "istors": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/istors",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/istors",
         "rel": "bookmark"
       }
     ],
     "capabilities": {},
     "operational": "disabled",
     "location": {
       "locn": ""
     },
     "invprovision": "provisioned",
     "administrative": "locked",
     "personality": "compute",
     "iinterfaces": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/iinterfaces",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/iinterfaces",
         "rel": "bookmark"
       }
     ],
     "pci_devices": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/pci_devices",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/pci_devices",
         "rel": "bookmark"
       }
     ],
     "ethernet_ports": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ethernet_ports",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ethernet_ports",
         "rel": "bookmark"
       }
     ],
     "mtce_info": null,
     "isensorgroups": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/isensorgroups",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/isensorgroups",
         "rel": "bookmark"
       }
     ],
     "isystem_uuid": "4d31d98d-4992-445a-b749-485ce6077fd2",
     "boot_device": "sda",
     "rootfs_device": "sda",
     "mgmt_mac": "08:00:27:a1:02:ff",
     "subfunction_oper": "disabled",
     "peers": null,
     "task": "",
     "ttys_dcd": "False",
     "target_load": "15.12",
     "lldp_neighbours": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/lldp_neighbours",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/lldp_neighbours",
         "rel": "bookmark"
       }
     ],
     "created_at": "2016-11-28T17:58:07.778282+00:00",
     "subfunction_avail": "online",
     "install_output": "graphical",
     "ipvs": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ipvs",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ipvs",
         "rel": "bookmark"
       }
     ],
     "ilvgs": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ilvgs",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ilvgs",
         "rel": "bookmark"
       }
     ],
     "action": "none",
     "bm_type": "",
     "lldp_agents": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/lldp_agents",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/lldp_agents",
         "rel": "bookmark"
       }
     ],
     "imemorys": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/imemorys",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/imemorys",
         "rel": "bookmark"
       }
     ],
     "ports": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ports",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/ports",
         "rel": "bookmark"
       }
     ],
     "inodes": [
       {
         "href": "http://10.10.10.2:6385/v1/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/inodes",
         "rel": "self"
       },
       {
         "href": "http://10.10.10.2:6385/ihosts/e6c1a877-a332-46dd-821d-e5fa9e2c4ade/inodes",
         "rel": "bookmark"
       }
     ],
     "config_target": null
   }

------
Ports
------

These APIs allow the display of the physical ports of a host and their
attributes.

***********************************
List the physical ports of a host
***********************************

.. rest_method:: GET /v1/ihosts/​{host_id}​/ports

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "ports (Optional)", "plain", "xsd:list", "The list of physical ports of a host."
   "pname (Optional)", "plain", "xsd:string", "The discovered name of the port, typically the Linux assigned device name, if available."
   "pnamedisplay (Optional)", "plain", "xsd:string", "The user-specified name for the port."
   "mac (Optional)", "plain", "xsd:string", "The MAC Address of the port."
   "pciaddr (Optional)", "plain", "xsd:string", "The PCI Address of the port."
   "speed (Optional)", "plain", "xsd:string", "Currently not supported."
   "autoneg (Optional)", "plain", "xsd:boolean", "Currently not supported."
   "mtu (Optional)", "plain", "xsd:integer", "The Maximum Transmission Unit (MTU) of the port, in bytes."
   "link_mode (Optional)", "plain", "xsd:string", "Currently not supported."
   "bootp (Optional)", "plain", "xsd:boolean", "Indicates whether the port can be used for network booting."
   "sriov_totalvfs (Optional)", "plain", "xsd:integer", "Indicates the maximum number of VFs that this port can support."
   "sriov_numvfs (Optional)", "plain", "xsd:integer", "Indicates the actual number of VFs configured for the interface using this port."
   "sriov_vfs_pci_address (Optional)", "plain", "xsd:string", "A comma-separated list of the PCI addresses of the configured VFs."
   "driver (Optional)", "plain", "xsd:string", "The driver being used for the port. Valid values are ``ixgbe`` and ``igb``."
   "pclass (Optional)", "plain", "xsd:string", "The class or type of the physical IO controller device of the port."
   "pvendor (Optional)", "plain", "xsd:string", "The primary vendor information of the port hardware."
   "psvendor (Optional)", "plain", "xsd:string", "The secondary vendor information of the port hardware."
   "pdevice (Optional)", "plain", "xsd:string", "The primary type and model information of the port hardware."
   "psdevice (Optional)", "plain", "xsd:string", "The secondary type and model information of the port hardware ."
   "iinterface_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the L2 interface of the port."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA Node of the port."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA node of the port."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host containing the port."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
     "ports": [
       {
         "links": [
           {
             "href": "http://192.168.204.2:6385/v1/ports/972dd648-bec0-4204-a469-75c745a5994e",
             "rel": "self"
           },
           {
             "href": "http://192.168.204.2:6385/ports/972dd648-bec0-4204-a469-75c745a5994e",
             "rel": "bookmark"
           }
         ],
         "inode_uuid": "e5d68519-eb07-4b28-8ca2-32bb476eeec1",
         "updated_at": "2014-09-18T03:12:15.389263+00:00",
         "ihost_uuid": "0aca08f9-882f-4491-8ffd-7368d20ead48",
         "autoneg": null,
         "speed": null,
         "iinterface_uuid": null,
         "uuid": "972dd648-bec0-4204-a469-75c745a5994e",
         "pdevice": "I350 Gigabit Network Connection",
         "capabilities": {

         },
         "psdevice": "Device 3592",
         "link_mode": 0,
         "bootp": null,
         "mac": "00:1e:67:51:50:01",
         "sriov_totalvfs": 63,
         "sriov_numvfs": 0,
         "sriov_vfs_pci_address": "",
         "driver": "ixgbe",
         "name": "eth0",
         "psvendor": "Intel Corporation",
         "numa_node": 0,
         "created_at": "2014-09-18T03:12:15.334214+00:00",
         "pclass": "Ethernet controller",
         "mtu": 1500,
         "pvendor": "Intel Corporation",
         "pciaddr": "0000:0a:00.0",
         "namedisplay": null
       },
       {
         "links": [
           {
             "href": "http://192.168.204.2:6385/v1/ports/c822edfe-af87-4a15-ac9b-6a8123caede1",
             "rel": "self"
           },
           {
             "href": "http://192.168.204.2:6385/ports/c822edfe-af87-4a15-ac9b-6a8123caede1",
             "rel": "bookmark"
           }
         ],
         "inode_uuid": "e5d68519-eb07-4b28-8ca2-32bb476eeec1",
         "updated_at": "2014-09-22T02:00:43.938843+00:00",
         "ihost_uuid": "0aca08f9-882f-4491-8ffd-7368d20ead48",
         "autoneg": null,
         "speed": null,
         "iinterface_uuid": "b24caa6c-71b1-42be-8968-89abd269ea82",
         "uuid": "c822edfe-af87-4a15-ac9b-6a8123caede1",
         "pdevice": "82599EB 10-Gigabit SFI/SFP+ Network Connection",
         "capabilities": {

         },
         "psdevice": "Ethernet Server Adapter X520-2",
         "link_mode": 0,
         "bootp": null,
         "mac": "90:e2:ba:39:bb:8c",
         "sriov_totalvfs": 63,
         "sriov_numvfs": 0,
         "sriov_vfs_pci_address": "",
         "driver": "ixgbe",
         "name": "eth4",
         "psvendor": "Intel Corporation",
         "numa_node": 0,
         "created_at": "2014-09-18T03:12:15.325296+00:00",
         "pclass": "Ethernet controller",
         "mtu": 1500,
         "pvendor": "Intel Corporation",
         "pciaddr": "0000:05:00.0",
         "namedisplay": null
       }
     ]
   }

This operation does not accept a request body.

**************************************************
Shows the attributes of a specific physical port
**************************************************

.. rest_method:: GET /v1/ports/​{port_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "port_id", "URI", "csapi:UUID", "The unique identifier of an existing port."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "pname (Optional)", "plain", "xsd:string", "The discovered name of the port, typically the Linux assigned device name, if available."
   "pnamedisplay (Optional)", "plain", "xsd:string", "The user-specified name for the port."
   "mac (Optional)", "plain", "xsd:string", "The MAC Address of the port."
   "pciaddr (Optional)", "plain", "xsd:string", "The PCI Address of the port."
   "speed (Optional)", "plain", "xsd:string", "Currently not supported."
   "autoneg (Optional)", "plain", "xsd:boolean", "Currently not supported."
   "mtu (Optional)", "plain", "xsd:integer", "The Maximum Transmission Unit (MTU) of the port, in bytes."
   "link_mode (Optional)", "plain", "xsd:string", "Currently not supported."
   "bootp (Optional)", "plain", "xsd:boolean", "Indicates whether the port can be used for network booting."
   "sriov_totalvfs (Optional)", "plain", "xsd:integer", "Indicates the maximum number of VFs that this port can support."
   "sriov_numvfs (Optional)", "plain", "xsd:integer", "Indicates the actual number of VFs configured for the interface using this port."
   "sriov_vfs_pci_address (Optional)", "plain", "xsd:string", "A comma-separated list of the PCI addresses of the configured VFs."
   "driver (Optional)", "plain", "xsd:string", "The driver being used for the port. Valid values are ``ixgbe`` and ``igb``."
   "pclass (Optional)", "plain", "xsd:string", "The class or type of the physical IO controller device of the port."
   "pvendor (Optional)", "plain", "xsd:string", "The primary vendor information of the port hardware."
   "psvendor (Optional)", "plain", "xsd:string", "The secondary vendor information of the port hardware."
   "pdevice (Optional)", "plain", "xsd:string", "The primary type and model information of the port hardware."
   "psdevice (Optional)", "plain", "xsd:string", "The secondary type and model information of the port hardware ."
   "iinterface_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the L2 interface of the port."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA Node of the port."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA node of the port."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host containing the port."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "pdevice" : "82599EB 10-Gigabit SFI/SFP+ Network Connection",
      "bootp" : null,
      "uuid" : "c822edfe-af87-4a15-ac9b-6a8123caede1",
      "ihost_uuid" : "0aca08f9-882f-4491-8ffd-7368d20ead48",
      "pvendor" : "Intel Corporation",
      "created_at" : "2014-09-18T03:12:15.325296+00:00",
      "speed" : null,
      "capabilities" : {},
      "mac" : "90:e2:ba:39:bb:8c",
      "sriov_totalvfs": 63,
      "sriov_numvfs": 0,
      "sriov_vfs_pci_address": "",
      "driver": "ixgbe",
      "mtu" : 1500,
      "links" : [
         {
            "rel" : "self",
            "href" : "http://128.224.151.243:6385/v1/ports/c822edfe-af87-4a15-ac9b-6a8123caede1"
         },
         {
            "rel" : "bookmark",
            "href" : "http://128.224.151.243:6385/ports/c822edfe-af87-4a15-ac9b-6a8123caede1"
         }
      ],
      "psvendor" : "Intel Corporation",
      "iinterface_uuid" : "b24caa6c-71b1-42be-8968-89abd269ea82",
      "numa_node" : 0,
      "pciaddr" : "0000:05:00.0",
      "pclass" : "Ethernet controller",
      "psdevice" : "Ethernet Server Adapter X520-2",
      "updated_at" : "2014-09-22T02:00:43.938843+00:00",
      "link_mode" : 0,
      "autoneg" : null,
      "pname" : "eth4",
      "pnamedisplay" : null,
      "inode_uuid" : "e5d68519-eb07-4b28-8ca2-32bb476eeec1"
   }

This operation does not accept a request body.

-----
CPUs
-----

These APIs allow the display of the logical core(s) of the processor(s)
on a host, and the display and modification of the cores assigned
function.

****************************************************
Lists all cpus (logical processor cores) of a host
****************************************************

.. rest_method:: GET /v1/ihosts/​{host_id}​/icpus

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "icpus (Optional)", "plain", "xsd:list", "The list of cpus (logical processor cores) of a host."
   "cpu (Optional)", "plain", "xsd:integer", "The logical core number."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA Node or physical processor device of the logical core."
   "core (Optional)", "plain", "xsd:integer", "The physical core of the logical core."
   "thread (Optional)", "plain", "xsd:integer", "The thread within the physical core of the logical core."
   "allocated_function (Optional)", "plain", "xsd:string", "The function assigned to this logical core; valid values are Platform, Vswitch, Shared or VMs . ``Platform`` indicates the core is used for the host kernel, StarlingX and OpenStack Services, ``Vswitch`` indicates the core is used by the virtual switch, ``Shared`` indicates that the core is reserved for sharing by VMs using the hw:wrs:shared_vcpu flavor extra spec, ``VMs`` indicates that the core is available for use by VMs."
   "cpu_family (Optional)", "plain", "xsd:string", "The CPU Family for the processor of the logical core."
   "cpu_model (Optional)", "plain", "xsd:string", "The vendor, model, part number and other info related to the processor device of the logical core."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host of the logical core."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA Node of the logical core."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "icpus" : [
         {
            "core" : 0,
            "allocated_function" : "Platform",
            "cpu" : 0,
            "numa_node" : 0,
            "uuid" : "d269a009-de03-463e-a553-10944361d38b",
            "cpu_family" : "6",
            "ihost_uuid" : "0aca08f9-882f-4491-8ffd-7368d20ead48",
            "thread" : 0,
            "created_at" : "2014-09-18T03:12:15.429976+00:00",
            "cpu_model" : "Intel(R) Xeon(R) CPU E5-2690 0 @ 2.90GHz",
            "updated_at" : null,
            "capabilities" : {},
            "links" : [
               {
                  "rel" : "self",
                  "href" : "http://128.224.151.243:6385/v1/icpus/d269a009-de03-463e-a553-10944361d38b"
               },
               {
                  "rel" : "bookmark",
                  "href" : "http://128.224.151.243:6385/icpus/d269a009-de03-463e-a553-10944361d38b"
               }
            ],
            "inode_uuid" : "e5d68519-eb07-4b28-8ca2-32bb476eeec1"
         },
         {
            "core" : 1,
            "allocated_function" : "Vswitch",
            "cpu" : 1,
            "numa_node" : 0,
            "uuid" : "f236a371-48e9-4618-8f02-3a7ea0c2d16e",
            "cpu_family" : "6",
            "ihost_uuid" : "0aca08f9-882f-4491-8ffd-7368d20ead48",
            "thread" : 0,
            "created_at" : "2014-09-18T03:12:15.432471+00:00",
            "cpu_model" : "Intel(R) Xeon(R) CPU E5-2690 0 @ 2.90GHz",
            "updated_at" : null,
            "capabilities" : {},
            "links" : [
               {
                  "rel" : "self",
                  "href" : "http://128.224.151.243:6385/v1/icpus/f236a371-48e9-4618-8f02-3a7ea0c2d16e"
               },
               {
                  "rel" : "bookmark",
                  "href" : "http://128.224.151.243:6385/icpus/f236a371-48e9-4618-8f02-3a7ea0c2d16e"
               }
            ],
            "inode_uuid" : "e5d68519-eb07-4b28-8ca2-32bb476eeec1"
         },

   ...

      ]
   }

This operation does not accept a request body.

*****************************************************************
Shows information about a specific cpu (logical processor core)
*****************************************************************

.. rest_method:: GET /v1/icpus/​{cpu_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "cpu_id", "URI", "csapi:UUID", "The unique identifier of a cpu (logical processor core)."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "cpu (Optional)", "plain", "xsd:integer", "The logical core number."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA Node or physical processor device of the logical core."
   "core (Optional)", "plain", "xsd:integer", "The physical core of the logical core."
   "thread (Optional)", "plain", "xsd:integer", "The thread within the physical core of the logical core."
   "allocated_function (Optional)", "plain", "xsd:string", "The function assigned to this logical core; valid values are Platform, Vswitch, Shared or VMs . ``Platform`` indicates the core is used for the host kernel, StarlingX and OpenStack Services, ``Vswitch`` indicates the core is used by the virtual switch, ``Shared`` indicates that the core is reserved for sharing by VMs using the hw:wrs:shared_vcpu flavor extra spec, ``VMs`` indicates that the core is available for use by VMs."
   "cpu_family (Optional)", "plain", "xsd:string", "The CPU Family for the processor of the logical core."
   "cpu_model (Optional)", "plain", "xsd:string", "The vendor, model, part number and other info related to the processor device of the logical core."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host of the logical core."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA Node of the logical core."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "core" : 0,
      "allocated_function" : "Platform",
      "function" : null,
      "uuid" : "d269a009-de03-463e-a553-10944361d38b",
      "cpu_family" : "6",
      "ihost_uuid" : "0aca08f9-882f-4491-8ffd-7368d20ead48",
      "created_at" : "2014-09-18T03:12:15.429976+00:00",
      "num_cores_on_processor1" : null,
      "capabilities" : {},
      "num_cores_on_processor3" : null,
      "links" : [
         {
            "rel" : "self",
            "href" : "http://128.224.151.243:6385/v1/icpus/d269a009-de03-463e-a553-10944361d38b"
         },
         {
            "rel" : "bookmark",
            "href" : "http://128.224.151.243:6385/icpus/d269a009-de03-463e-a553-10944361d38b"
         }
      ],
      "num_cores_on_processor2" : null,
      "cpu" : 0,
      "numa_node" : 0,
      "num_cores_on_processor0" : null,
      "thread" : 0,
      "cpu_model" : "Intel(R) Xeon(R) CPU E5-2690 0 @ 2.90GHz",
      "updated_at" : null,
      "inode_uuid" : "e5d68519-eb07-4b28-8ca2-32bb476eeec1"
   }

This operation does not accept a request body.

************************************************************************
Modifies the number of cores assigned to different functions on a host
************************************************************************

.. rest_method:: PUT /v1/ihosts/​{host_id}​/state/host_cpus_modify

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."
   "function (Optional)", "plain", "xsd:string", "This parameter specifies the function that is being assigned a different number of cores. The only functions currently allowed to be assigned a different number of cores ``platform``, ``vswitch`` and ``shared``. ``platform`` function is for managing the cores dedicated to the platform. ``vswitch`` function is for managing the cores dedicated to the vswitch. ``shared`` function is for managing the cores reserved for sharing by VMs using the hw:wrs:shared_vcpu flavor extra spec."
   "sockets (Optional)", "plain", "xsd:list", "The number of cores on a socket assigned to this function."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "cpu (Optional)", "plain", "xsd:integer", "The logical core number."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA Node or physical processor device of the logical core."
   "core (Optional)", "plain", "xsd:integer", "The physical core of the logical core."
   "thread (Optional)", "plain", "xsd:integer", "The thread within the physical core of the logical core."
   "allocated_function (Optional)", "plain", "xsd:string", "The function assigned to this logical core; valid values are Platform, Vswitch, Shared or VMs . ``Platform`` indicates the core is used for the host kernel, StarlingX and OpenStack Services, ``Vswitch`` indicates the core is used by the virtual switch, ``Shared`` indicates that the core is reserved for sharing by VMs using the hw:wrs:shared_vcpu flavor extra spec, ``VMs`` indicates that the core is available for use by VMs."
   "cpu_family (Optional)", "plain", "xsd:string", "The CPU Family for the processor of the logical core."
   "cpu_model (Optional)", "plain", "xsd:string", "The vendor, model, part number and other info related to the processor device of the logical core."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host of the logical core."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA Node of the logical core."

::

   [
     {
       "function": "vswitch",
       "sockets": [{"0": 1}]
     }
   ]

::

   [
      {
         "function": "platform",
         "sockets": [{"0": 1}, {"1": 1}],
      }
   ]

::

   {
     "allocated_function": "VMs",
     "core": 7,
     "thread": 0,
     "links": [
       {
         "href": "http://192.168.204.2:6385/v1/icpus/8a960696-c242-436f-a79c-d904fa6dcbd2",
         "rel": "self"
       },
       {
         "href": "http://192.168.204.2:6385/icpus/8a960696-c242-436f-a79c-d904fa6dcbd2",
         "rel": "bookmark"
       }
     ],
     "inode_uuid": "4ed560e2-c3a1-4d41-8b00-6af257e6ac75",
     "function": null,
     "numa_node": 1,
     "created_at": "2014-09-26T02:01:36.514217+00:00",
     "cpu_model": "Intel(R) Xeon(R) CPU E5-2670 0 @ 2.60GHz",
     "capabilities": {

     },
     "updated_at": "2014-09-26T11:58:20.586235+00:00",
     "num_cores_on_processor1": null,
     "ihost_uuid": "22d5827c-7a04-4a3c-9509-e8849b9a595d",
     "num_cores_on_processor3": null,
     "num_cores_on_processor2": null,
     "num_cores_on_processor0": null,
     "cpu_family": "6",
     "cpu": 15,
     "uuid": "8a960696-c242-436f-a79c-d904fa6dcbd2"
   }

-------
Memory
-------

These APIs allow the display of the size and usage of various memory
areas of the NUMA nodes of a host. The modification of the size of these
memory areas is also supported through these APIs. The different memory
areas of a NUMA node of a host are:

-  Memory reserved for the Platform; where the Platform consists of the
   kernel and the cloud services,

-  Memory reserved for the virtual switch (Note: only on 'compute'
   hosts),

-  Memory reserved for the hosted VMs (Note: only on 'compute' hosts).

**********************************************************
Lists the memory information of all NUMA nodes of a host
**********************************************************

.. rest_method:: GET /v1/ihosts/​{host_id}​/imemorys

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "imemorys (Optional)", "plain", "xsd:list", "The list of NUMA nodes (and their associated memory information) for this host."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA node number."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA node."
   "platform_reserved_mib (Optional)", "plain", "xsd:integer", "Memory reserved for the Kernel and Cloud Platform Services, in MiBs."
   "memtotal_mib (Optional)", "plain", "xsd:integer", "Total memory reserved for the hosted Virtual Machines, in MiBs."
   "memavail_mib (Optional)", "plain", "xsd:integer", "Free / available memory from the total memory reserved for the hosted Virtual Machines, in MiBs."
   "hugepages_configured (Optional)", "plain", "xsd:boolean", "Boolean indicating whether huge page memory is configured or not."
   "vswitch_hugepages_size_mib (Optional)", "plain", "xsd:integer", "The size of a Virtual Switch (vSwitch) huge page, in MiBs."
   "vswitch_hugepages_nr (Optional)", "plain", "xsd:integer", "The total number of Virtual Switch (vSwitch) huge pages."
   "vswitch_hugepages_avail (Optional)", "plain", "xsd:integer", "The free / available Virtual Switch (vSwitch) huge pages."
   "vm_hugepages_nr_1G (Optional)", "plain", "xsd:integer", "The total number of Virtual Machine 1G huge pages."
   "vm_hugepages_avail_1G (Optional)", "plain", "xsd:integer", "The free / available Virtual Machine 1G huge pages."
   "vm_hugepages_nr_1G_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 1G huge pages."
   "vm_hugepages_nr_2M (Optional)", "plain", "xsd:integer", "The total number of Virtual Machine 2M huge pages."
   "vm_hugepages_avail_2M (Optional)", "plain", "xsd:integer", "The free / available Virtual Machine 2M huge pages."
   "vm_hugepages_nr_2M_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 2M huge pages."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "imemorys" : [
         {
            "vswitch_hugepages_nr" : 1,
            "hugepages_configured" : "True",
            "vm_hugepages_nr_1G_pending" : null,
            "memavail_mib" : 22448,
            "uuid" : "34098f3a-6b95-4cad-aecb-986dc6312f4f",
            "ihost_uuid" : "afecdcfb-2954-498d-88bf-d1385b00f34d",
            "created_at" : "2015-04-06T20:27:50.171841+00:00",
            "vswitch_hugepages_size_mib" : 1024,
            "vm_hugepages_avail_1G" : 0,
            "capabilities" : {},
            "links" : [
               {
                  "rel" : "self",
                  "href" : "http://128.224.150.54:6385/v1/imemorys/34098f3a-6b95-4cad-aecb-986dc6312f4f"
               },
               {
                  "rel" : "bookmark",
                  "href" : "http://128.224.150.54:6385/imemorys/34098f3a-6b95-4cad-aecb-986dc6312f4f"
               }
            ],
            "vm_hugepages_nr_2M_pending" : null,
            "vswitch_hugepages_reqd" : null,
            "vm_hugepages_avail_2M" : 11224,
            "vswitch_hugepages_avail" : 0,
            "numa_node" : 0,
            "vm_hugepages_nr_1G" : 0,
            "updated_at" : "2015-04-08T11:32:25.205552+00:00",
            "platform_reserved_mib" : 4000,
            "memtotal_mib" : 27056,
            "vm_hugepages_nr_2M" : 13016,
            "inode_uuid" : "c65c852c-1707-40e1-abfc-334270ec0427"
         },
         {
            "vswitch_hugepages_nr" : 1,
            "hugepages_configured" : "True",
            "vm_hugepages_nr_1G_pending" : null,
            "memavail_mib" : 24082,
            "uuid" : "85df5109-77d9-4335-9181-0efa82c98dcc",
            "ihost_uuid" : "afecdcfb-2954-498d-88bf-d1385b00f34d",
            "created_at" : "2015-04-06T20:27:50.182764+00:00",
            "vswitch_hugepages_size_mib" : 1024,
            "vm_hugepages_avail_1G" : 0,
            "capabilities" : {},
            "links" : [
               {
                  "rel" : "self",
                  "href" : "http://128.224.150.54:6385/v1/imemorys/85df5109-77d9-4335-9181-0efa82c98dcc"
               },
               {
                  "rel" : "bookmark",
                  "href" : "http://128.224.150.54:6385/imemorys/85df5109-77d9-4335-9181-0efa82c98dcc"
               }
            ],
            "vm_hugepages_nr_2M_pending" : null,
            "vswitch_hugepages_reqd" : null,
            "vm_hugepages_avail_2M" : 12041,
            "vswitch_hugepages_avail" : 0,
            "numa_node" : 1,
            "vm_hugepages_nr_1G" : 0,
            "updated_at" : "2015-04-08T11:32:25.220242+00:00",
            "platform_reserved_mib" : 2000,
            "memtotal_mib" : 29202,
            "vm_hugepages_nr_2M" : 14089,
            "inode_uuid" : "67d3c9a0-57b2-4532-b7ea-f1cd16a3b349"
         }
      ]
   }

This operation does not accept a request body.

****************************************************************************
Shows the memory information about a specific NUMA node of a specific host
****************************************************************************

.. rest_method:: GET /v1/imemorys/​{memory_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "memory_id", "URI", "csapi:UUID", "The unique identifier of a memory area."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA node number."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA node."
   "platform_reserved_mib (Optional)", "plain", "xsd:integer", "Memory reserved for the Kernel and Cloud Platform Services, in MiBs."
   "memtotal_mib (Optional)", "plain", "xsd:integer", "Total memory reserved for the hosted Virtual Machines, in MiBs."
   "memavail_mib (Optional)", "plain", "xsd:integer", "Free / available memory from the total memory reserved for the hosted Virtual Machines, in MiBs."
   "hugepages_configured (Optional)", "plain", "xsd:boolean", "Boolean indicating whether huge page memory is configured or not."
   "vswitch_hugepages_size_mib (Optional)", "plain", "xsd:integer", "The size of a Virtual Switch (vSwitch) huge page, in MiBs."
   "vswitch_hugepages_nr (Optional)", "plain", "xsd:integer", "The total number of Virtual Switch (vSwitch) huge pages."
   "vswitch_hugepages_avail (Optional)", "plain", "xsd:integer", "The free / available Virtual Switch (vSwitch) huge pages."
   "vm_hugepages_nr_1G (Optional)", "plain", "xsd:integer", "The total number of Virtual Machine 1G huge pages."
   "vm_hugepages_avail_1G (Optional)", "plain", "xsd:integer", "The free / available Virtual Machine 1G huge pages."
   "vm_hugepages_nr_1G_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 1G huge pages."
   "vm_hugepages_nr_2M (Optional)", "plain", "xsd:integer", "The total number of Virtual Machine 2M huge pages."
   "vm_hugepages_avail_2M (Optional)", "plain", "xsd:integer", "The free / available Virtual Machine 2M huge pages."
   "vm_hugepages_nr_2M_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 2M huge pages."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "vswitch_hugepages_nr" : 1,
      "hugepages_configured" : "True",
      "vm_hugepages_nr_1G_pending" : null,
      "memavail_mib" : 24082,
      "uuid" : "85df5109-77d9-4335-9181-0efa82c98dcc",
      "ihost_uuid" : "afecdcfb-2954-498d-88bf-d1385b00f34d",
      "created_at" : "2015-04-06T20:27:50.182764+00:00",
      "vswitch_hugepages_size_mib" : 1024,
      "vm_hugepages_avail_1G" : 0,
      "capabilities" : {},
      "links" : [
         {
            "rel" : "self",
            "href" : "http://128.224.150.54:6385/v1/imemorys/85df5109-77d9-4335-9181-0efa82c98dcc"
         },
         {
            "rel" : "bookmark",
            "href" : "http://128.224.150.54:6385/imemorys/85df5109-77d9-4335-9181-0efa82c98dcc"
         }
      ],
      "vm_hugepages_nr_2M_pending" : null,
      "vswitch_hugepages_reqd" : null,
      "vm_hugepages_avail_2M" : 12041,
      "vswitch_hugepages_avail" : 0,
      "numa_node" : 1,
      "vm_hugepages_nr_1G" : 0,
      "updated_at" : "2015-04-08T11:33:25.280674+00:00",
      "platform_reserved_mib" : 2000,
      "memtotal_mib" : 29202,
      "vm_hugepages_nr_2M" : 14089,
      "inode_uuid" : "67d3c9a0-57b2-4532-b7ea-f1cd16a3b349"
   }

This operation does not accept a request body.

********************************************************************************
Modifies the memory information about a specific NUMA node of a specific host.
********************************************************************************

.. rest_method:: PATCH /v1/imemorys/​{memory_id}​

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "memory_id", "URI", "csapi:UUID", "The unique identifier of a memory area."
   "platform_reserved_mib (Optional)", "plain", "xsd:integer", "If not null, the amount of reserved memory for platform in MiB"
   "vm_hugepages_nr_1G_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 1G huge pages."
   "vm_hugepages_nr_2M_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 2M huge pages."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host."
   "numa_node (Optional)", "plain", "xsd:integer", "The NUMA node number."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the NUMA node."
   "platform_reserved_mib (Optional)", "plain", "xsd:integer", "Memory reserved for the Kernel and Cloud Platform Services, in MiBs."
   "memtotal_mib (Optional)", "plain", "xsd:integer", "Total memory reserved for the hosted Virtual Machines, in MiBs."
   "memavail_mib (Optional)", "plain", "xsd:integer", "Free / available memory from the total memory reserved for the hosted Virtual Machines, in MiBs."
   "hugepages_configured (Optional)", "plain", "xsd:boolean", "Boolean indicating whether huge page memory is configured or not."
   "vswitch_hugepages_size_mib (Optional)", "plain", "xsd:integer", "The size of a Virtual Switch (vSwitch) huge page, in MiBs."
   "vswitch_hugepages_nr (Optional)", "plain", "xsd:integer", "The total number of Virtual Switch (vSwitch) huge pages."
   "vswitch_hugepages_avail (Optional)", "plain", "xsd:integer", "The free / available Virtual Switch (vSwitch) huge pages."
   "vm_hugepages_nr_1G (Optional)", "plain", "xsd:integer", "The total number of Virtual Machine 1G huge pages."
   "vm_hugepages_avail_1G (Optional)", "plain", "xsd:integer", "The free / available Virtual Machine 1G huge pages."
   "vm_hugepages_nr_1G_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 1G huge pages."
   "vm_hugepages_nr_2M (Optional)", "plain", "xsd:integer", "The total number of Virtual Machine 2M huge pages."
   "vm_hugepages_avail_2M (Optional)", "plain", "xsd:integer", "The free / available Virtual Machine 2M huge pages."
   "vm_hugepages_nr_2M_pending (Optional)", "plain", "xsd:integer", "If not null, the pending configured number of Virtual Machine 2M huge pages."

::

   [
     {
       "path": "/platform_reserved_mib",
       "value": "2000",
       "op": "replace"
     },
     {
       "path": "/vm_hugepages_nr_1G_pending",
       "value": "100",
       "op": "replace"
     },
     {
       "path": "/vm_hugepages_nr_2M_pending",
       "value": "50",
       "op": "replace"
     }
   ]

::

   {
      "vswitch_hugepages_nr" : 1,
      "hugepages_configured" : "True",
      "vm_hugepages_nr_1G_pending" : null,
      "memavail_mib" : 24082,
      "uuid" : "85df5109-77d9-4335-9181-0efa82c98dcc",
      "ihost_uuid" : "afecdcfb-2954-498d-88bf-d1385b00f34d",
      "created_at" : "2015-04-06T20:27:50.182764+00:00",
      "vswitch_hugepages_size_mib" : 1024,
      "vm_hugepages_avail_1G" : 0,
      "capabilities" : {},
      "links" : [
         {
            "rel" : "self",
            "href" : "http://128.224.150.54:6385/v1/imemorys/85df5109-77d9-4335-9181-0efa82c98dcc"
         },
         {
            "rel" : "bookmark",
            "href" : "http://128.224.150.54:6385/imemorys/85df5109-77d9-4335-9181-0efa82c98dcc"
         }
      ],
      "vm_hugepages_nr_2M_pending" : null,
      "vswitch_hugepages_reqd" : null,
      "vm_hugepages_avail_2M" : 12041,
      "vswitch_hugepages_avail" : 0,
      "numa_node" : 1,
      "vm_hugepages_nr_1G" : 0,
      "updated_at" : "2015-04-08T11:33:25.280674+00:00",
      "platform_reserved_mib" : 2000,
      "memtotal_mib" : 29202,
      "vm_hugepages_nr_2M" : 14089,
      "inode_uuid" : "67d3c9a0-57b2-4532-b7ea-f1cd16a3b349"
   }

------
Disks
------

************************************
Lists all physical disks of a host
************************************

.. rest_method:: GET /v1/ihosts/​{host_id}​/idisks

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "idisks (Optional)", "plain", "xsd:list", "The list of physical disk entities."
   "capabilities (Optional)", "plain", "xsd:string", "Additional capabilities info about the disk."
   "device_node (Optional)", "plain", "xsd:string", "The device node of the disk."
   "device_path (Optional)", "plain", "xsd:string", "The device path of the disk."
   "device_id (Optional)", "plain", "xsd:string", "The device ID of the disk."
   "device_wwn (Optional)", "plain", "xsd:string", "The device WWN of the disk."
   "device_num (Optional)", "plain", "xsd:integer", "The device number of the disk."
   "device_type (Optional)", "plain", "xsd:string", "The disk device type."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The host UUID that the disk belongs to."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The node UUID that the disk belongs to."
   "istor_uuid (Optional)", "plain", "csapi:UUID", "The logical storage function that this disk belongs to."
   "ipv_uuid (Optional)", "plain", "csapi:UUID", "The LVM physical volume that this disk belongs to."
   "serial_id (Optional)", "plain", "xsd:string", "The serial id or number of the disk."
   "rpm (Optional)", "plain", "xsd:string", "The RPM of the disk. ""Undetermined"" if not specified. ""N/A"", not applicable for SSDs or NVME disks."
   "size_mib (Optional)", "plain", "xsd:integer", "The size of the disk in MiBytes."
   "available_mib (Optional)", "plain", "xsd:integer", "The unpartitioned size of the disk in MiBytes."

::

   {
       "idisks": [
           {
               "device_path": "/dev/disk/by-path/pci-0000:00:0d.0-ata-1.0",
               "uuid": "8352385e-b13b-488c-abca-f38db6a5e234",
               "links": [
                   {
                       "href": "http://10.10.10.2:6385/v1/idisks/8352385e-b13b-488c-abca-f38db6a5e234",
                       "rel": "self"
                   },
                   {
                       "href": "http://10.10.10.2:6385/idisks/8352385e-b13b-488c-abca-f38db6a5e234",
                       "rel": "bookmark"
                   }
               ],
               "ihost_uuid": "422f7a16-90b7-49b9-856e-e7a2527e3da1",
               "created_at": "2018-02-06T07:04:49.098057+00:00",
               "updated_at": "2018-02-06T08:44:03.928696+00:00",
               "device_node": "/dev/sda",
               "available_mib": 0,
               "ipv_uuid": null,
               "serial_id": "VB7f149a22-bb415a22",
               "device_type": "HDD",
               "device_wwn": null,
               "istor_uuid": null,
               "device_num": 2048,
               "capabilities": {
                   "model_num": "VBOX HARDDISK",
                   "stor_function": "rootfs"
               },
               "rpm": "Undetermined",
               "size_mib": 61440,
               "device_id": "ata-VBOX_HARDDISK_VB7f149a22-bb415a22"
           },
           {
               "device_path": "/dev/disk/by-path/pci-0000:00:0d.0-ata-2.0",
               "uuid": "0612ae5a-4b16-47a4-bb2b-d2776e6c0959",
               "links": [
                   {
                       "href": "http://10.10.10.2:6385/v1/idisks/0612ae5a-4b16-47a4-bb2b-d2776e6c0959",
                       "rel": "self"
                   },
                   {
                       "href": "http://10.10.10.2:6385/idisks/0612ae5a-4b16-47a4-bb2b-d2776e6c0959",
                       "rel": "bookmark"
                   }
               ],
               "ihost_uuid": "422f7a16-90b7-49b9-856e-e7a2527e3da1",
               "created_at": "2018-02-06T07:04:49.127350+00:00",
               "updated_at": "2018-02-06T08:44:03.987878+00:00",
               "device_node": "/dev/sdb",
               "available_mib": 51197,
               "ipv_uuid": null,
               "serial_id": "VBa3c3ba49-6d2fb877",
               "device_type": "HDD",
               "device_wwn": null,
               "istor_uuid": null,
               "device_num": 2064,
               "capabilities": {
                   "model_num": "VBOX HARDDISK"
               },
               "rpm": "Undetermined",
               "size_mib": 61440,
               "device_id": "ata-VBOX_HARDDISK_VBa3c3ba49-6d2fb877"
           }
       ]
   }

This operation does not accept a request body.

***********************************************************
Shows detailed information about a specific physical disk
***********************************************************

.. rest_method:: GET /v1/idisks/​{disk_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "disk_id", "URI", "csapi:UUID", "The unique identifier of a physical disk."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "capabilities (Optional)", "plain", "xsd:string", "Additional capabilities info about the disk."
   "device_node (Optional)", "plain", "xsd:string", "The device node of the disk."
   "device_path (Optional)", "plain", "xsd:string", "The device path of the disk."
   "device_id (Optional)", "plain", "xsd:string", "The device ID of the disk."
   "device_wwn (Optional)", "plain", "xsd:string", "The device WWN of the disk."
   "device_num (Optional)", "plain", "xsd:integer", "The device number of the disk."
   "device_type (Optional)", "plain", "xsd:string", "The disk device type."
   "ihost_uuid (Optional)", "plain", "csapi:UUID", "The host UUID that the disk belongs to."
   "inode_uuid (Optional)", "plain", "csapi:UUID", "The node UUID that the disk belongs to."
   "istor_uuid (Optional)", "plain", "csapi:UUID", "The logical storage function that this disk belongs to."
   "ipv_uuid (Optional)", "plain", "csapi:UUID", "The LVM physical volume that this disk belongs to."
   "serial_id (Optional)", "plain", "xsd:string", "The serial id or number of the disk."
   "rpm (Optional)", "plain", "xsd:string", "The RPM of the disk. ""Undetermined"" if not specified. ""N/A"", not applicable for SSDs or NVME disks."
   "size_mib (Optional)", "plain", "xsd:integer", "The size of the disk in MiBytes."
   "available_mib (Optional)", "plain", "xsd:integer", "The unpartitioned size of the disk in MiBytes."

::

   {
       "device_path": "/dev/disk/by-path/pci-0000:00:0d.0-ata-1.0",
       "uuid": "8352385e-b13b-488c-abca-f38db6a5e234",
       "links": [
           {
               "href": "http://10.10.10.2:6385/v1/idisks/8352385e-b13b-488c-abca-f38db6a5e234",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/idisks/8352385e-b13b-488c-abca-f38db6a5e234",
               "rel": "bookmark"
           }
       ],
       "ihost_uuid": "422f7a16-90b7-49b9-856e-e7a2527e3da1",
       "partitions": [
           {
               "href": "http://10.10.10.2:6385/v1/idisks/8352385e-b13b-488c-abca-f38db6a5e234/partitions",
               "rel": "self"
           },
           {
               "href": "http://10.10.10.2:6385/idisks/8352385e-b13b-488c-abca-f38db6a5e234/partitions",
               "rel": "bookmark"
           }
       ],
       "updated_at": "2018-02-06T08:45:03.851208+00:00",
       "device_node": "/dev/sda",
       "available_mib": 0,
       "ipv_uuid": null,
       "serial_id": "VB7f149a22-bb415a22",
       "device_type": "HDD",
       "device_wwn": null,
       "istor_uuid": null,
       "device_num": 2048,
       "capabilities": {
           "model_num": "VBOX HARDDISK",
           "stor_function": "rootfs"
       },
       "rpm": "Undetermined",
       "created_at": "2018-02-06T07:04:49.098057+00:00",
       "size_mib": 61440,
       "device_id": "ata-VBOX_HARDDISK_VB7f149a22-bb415a22"
   }

This operation does not accept a request body.

*******************
Modifies the disk
*******************

.. rest_method:: PATCH /v1/idisks/​{disk_id}​

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "disk_id", "URI", "csapi:UUID", "The unique identifier of a physical disk."
   "partition_table (Optional)", "plain", "xsd:string", "Set the partition table type to wipe and format the disk. Supported values are ``gpt``."

::

   [
       {
           "path": "/partition_table",
           "value": "gpt",
           "op": "replace"
       }
   ]

------------
SensorGroup
------------

These APIs allow the display of the operational state and configuration
attributes of the sensorgroups of a host. The modification of certain
sensorgroup attributes is supported through these APIs; and propagates
the configuration change to the corresponding attributes of all sensors
defined in the group. Examples of sensorgroup sensortype monitoring for
the host are:

-  temperature,

-  voltage,

-  current,

-  fan,

-  cpu,

-  memory,

-  disk,

-  partition,

-  firmware baseline,

-  hardware baseline.

*********************************************
Lists the sensorgroup information of a host
*********************************************

.. rest_method:: GET /v1/ihosts/​{host_id}​/isensorgroups

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "isensorgroups (Optional)", "plain", "xsd:list", "The list of associated sensorgroup information for this host."
   "sensorgroupname (Optional)", "plain", "xsd:string", "The name of the sensorgroup."
   "path (Optional)", "plain", "xsd:string", "The entity path of the sensorgroup."
   "sensortype (Optional)", "plain", "xsd:string", "The sensortype of the sensors in the sensorgroup. e.g. ``temperature, voltage, current, fan, power, disk, watchdog, memory, interrupt, firmware, hardware``."
   "datatype (Optional)", "plain", "xsd:string", "The sensor datatype of the sensors in the sensorgroup. e.g. ``discrete or analog``"
   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host."
   "suppress (Optional)", "plain", "xsd:boolean", "Specifies whether sensorgroup actions are suppressed. ``True`` specifies suppressed. This attribute is user configurable and affects all sensors within the sensorgroup."
   "audit_interval_group (Optional)", "plain", "xsd:boolean", "Specifies the audit interval time in seconds for the system sensor monitoring algorithm. This attribute is user configurable and affects all sensors within the sensorgroup."
   "t_critical_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Lower Threshold of the SensorGroup."
   "t_critical_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Upper Threshold of the SensorGroup."
   "t_major_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Lower Threshold of the SensorGroup."
   "t_critical_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Upper Threshold of the SensorGroup."
   "t_minor_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Lower Threshold of the SensorGroup."
   "t_minor_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Upper Threshold of the SensorGroup."
   "unit_base_group (Optional)", "plain", "xsd:string", "The unit base of the sensor. e.g. revolutions, C (Celcius), etc."
   "unit_modifier_group (Optional)", "plain", "xsd:string", "The unit modifier of the sensor. e.g. 10^2 (\*100)."
   "unit_rate_group (Optional)", "plain", "xsd:string", "The unit rate of the sensor. e.g. /h (per hour)."
   "algorithm (Optional)", "plain", "xsd:string", "The system sensor algorithm version information."
   "actions_critical_group (Optional)", "plain", "xsd:string", "The actions to take upon critical threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "actions_major_group (Optional)", "plain", "xsd:string", "The actions to take upon major threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "actions_minor_group (Optional)", "plain", "xsd:string", "The actions to take upon minor threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "isensorgroups":[
         {
            "audit_interval_group":null,
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b",
                  "rel":"bookmark"
               }
            ],
            "t_critical_upper_group":"120",
            "updated_at":"2015-09-08T13:37:36.426408+00:00",
            "isensors":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b/isensors",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b/isensors",
                  "rel":"bookmark"
               }
            ],
            "t_critical_lower_group":"-40",
            "t_minor_upper_group":"85",
            "t_minor_lower_group":"-10",
            "uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
            "unit_modifier_group":"1",
            "capabilities":{

            },
            "state":null,
            "unit_rate_group":null,
            "actions_major_group":"alarm",
            "suppress":"False",
            "actions_minor_group":"ignore",
            "sensorgroupname":"cpuTemp",
            "sensors":null,
            "actions_configurable":null,
            "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
            "t_major_lower_group":"-20",
            "unit_base_group":"Celcius",
            "sensortype":"temperature",
            "algorithm":null,
            "datatype":"analog",
            "possible_states":null,
            "created_at":"2015-09-08T12:46:36.808611+00:00",
            "actions_critical_group":"alarm",
            "t_major_upper_group":"10"
         },
         {
            "actions_minor_group":"ignore",
            "audit_interval_group":60,
            "actions_major_group":"alarm",
            "uuid":"3d05da03-d973-465f-bba1-ddbc14a4f36e",
            "algorithm":null,
            "datatype":"discrete",
            "possible_states":null,
            "created_at":"2015-09-08T13:48:27.615374+00:00",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensorgroups/3d05da03-d973-465f-bba1-ddbc14a4f36e",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensorgroups/3d05da03-d973-465f-bba1-ddbc14a4f36e",
                  "rel":"bookmark"
               }
            ],
            "capabilities":{

            },
            "updated_at":"2015-09-08T13:49:47.098184+00:00",
            "sensortype":"watchdog",
            "sensorgroupname":"watchdogSystem",
            "state":null,
            "isensors":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensorgroups/3d05da03-d973-465f-bba1-ddbc14a4f36e/isensors",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensorgroups/3d05da03-d973-465f-bba1-ddbc14a4f36e/isensors",
                  "rel":"bookmark"
               }
            ],
            "suppress":"False",
            "sensors":null,
            "actions_configurable":"alarm",
            "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
            "actions_critical_group":"alarm"
         }
      ]
   }

This operation does not accept a request body.

*************************************************************
Shows the sensorgroup information of a specific sensorgroup
*************************************************************

.. rest_method:: GET /v1/isensorgroups/​{sensorgroup_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensorgroup_id", "URI", "csapi:UUID", "The unique identifier of a sensorgroup."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensorgroupname (Optional)", "plain", "xsd:string", "The name of the sensorgroup."
   "path (Optional)", "plain", "xsd:string", "The entity path of the sensorgroup."
   "sensortype (Optional)", "plain", "xsd:string", "The sensortype of the sensors in the sensorgroup. e.g. ``temperature, voltage, current, fan, power, disk, watchdog, memory, interrupt, firmware, hardware``."
   "datatype (Optional)", "plain", "xsd:string", "The sensor datatype of the sensors in the sensorgroup. e.g. ``discrete or analog``"
   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host."
   "suppress (Optional)", "plain", "xsd:boolean", "Specifies whether sensorgroup actions are suppressed. ``True`` specifies suppressed. This attribute is user configurable and affects all sensors within the sensorgroup."
   "audit_interval_group (Optional)", "plain", "xsd:boolean", "Specifies the audit interval time in seconds for the system sensor monitoring algorithm. This attribute is user configurable and affects all sensors within the sensorgroup."
   "t_critical_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Lower Threshold of the SensorGroup."
   "t_critical_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Upper Threshold of the SensorGroup."
   "t_major_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Lower Threshold of the SensorGroup."
   "t_critical_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Upper Threshold of the SensorGroup."
   "t_minor_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Lower Threshold of the SensorGroup."
   "t_minor_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Upper Threshold of the SensorGroup."
   "unit_base_group (Optional)", "plain", "xsd:string", "The unit base of the sensor. e.g. revolutions, C (Celcius), etc."
   "unit_modifier_group (Optional)", "plain", "xsd:string", "The unit modifier of the sensor. e.g. 10^2 (\*100)."
   "unit_rate_group (Optional)", "plain", "xsd:string", "The unit rate of the sensor. e.g. /h (per hour)."
   "algorithm (Optional)", "plain", "xsd:string", "The system sensor algorithm version information."
   "actions_critical_group (Optional)", "plain", "xsd:string", "The actions to take upon critical threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "actions_major_group (Optional)", "plain", "xsd:string", "The actions to take upon major threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "actions_minor_group (Optional)", "plain", "xsd:string", "The actions to take upon minor threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "audit_interval_group":null,
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b",
            "rel":"bookmark"
         }
      ],
      "t_critical_upper_group":"120",
      "updated_at":"2015-09-08T13:37:36.426408+00:00",
      "isensors":[
         {
            "href":"http://192.168.204.2:6385/v1/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b/isensors",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b/isensors",
            "rel":"bookmark"
         }
      ],
      "t_critical_lower_group":"-40",
      "t_minor_upper_group":"85",
      "t_minor_lower_group":"-10",
      "uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
      "unit_modifier_group":"1",
      "capabilities":{

      },
      "state":null,
      "unit_rate_group":null,
      "actions_major_group":"alarm",
      "suppress":"False",
      "actions_minor_group":"ignore",
      "sensorgroupname":"cpuTemp",
      "sensors":null,
      "actions_configurable":null,
      "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
      "t_major_lower_group":"-20",
      "unit_base_group":"Celcius",
      "sensortype":"temperature",
      "algorithm":null,
      "datatype":"analog",
      "possible_states":null,
      "created_at":"2015-09-08T12:46:36.808611+00:00",
      "actions_critical_group":"alarm",
      "t_major_upper_group":"10"
   }

This operation does not accept a request body.

****************************************************************
Modifies the sensorgroup information of a specific sensorgroup
****************************************************************

.. rest_method:: PATCH /v1/isensorgroups/​{sensorgroup_id}​

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensorgroup_id", "URI", "csapi:UUID", "The unique identifier of a sensorgroup."
   "suppress (Optional)", "plain", "xsd:boolean", "If ``True``, suppresses any actions configured for the sensorgroup. The sensorgroup remains monitored, but the actions configured will be suppressed."
   "actions_critical_group (Optional)", "plain", "xsd:string", "Specifies the actions to take upon critical threshold event. Action selectable from actions_critical_choices. e.g. ``alarm, ignore, log, reset, powercycle``."
   "actions_major_group (Optional)", "plain", "xsd:string", "Specifies the actions to take upon major threshold event. Action selectable from actions_major_choices. e.g. ``alarm, ignore, log``."
   "actions_minor_group (Optional)", "plain", "xsd:string", "Specifies the actions to take upon minor threshold event. Action selectable from actions_minor_choices. e.g. ``ignore, log, alarm``."
   "audit_interval_group (Optional)", "plain", "xsd:integer", "Specifies the audit interval, in time-units of seconds, for the sensors in the sensorgroup."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensorgroupname (Optional)", "plain", "xsd:string", "The name of the sensorgroup."
   "path (Optional)", "plain", "xsd:string", "The entity path of the sensorgroup."
   "sensortype (Optional)", "plain", "xsd:string", "The sensortype of the sensors in the sensorgroup. e.g. ``temperature, voltage, current, fan, power, disk, watchdog, memory, interrupt, firmware, hardware``."
   "datatype (Optional)", "plain", "xsd:string", "The sensor datatype of the sensors in the sensorgroup. e.g. ``discrete or analog``"
   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host."
   "suppress (Optional)", "plain", "xsd:boolean", "Specifies whether sensorgroup actions are suppressed. ``True`` specifies suppressed. This attribute is user configurable and affects all sensors within the sensorgroup."
   "audit_interval_group (Optional)", "plain", "xsd:boolean", "Specifies the audit interval time in seconds for the system sensor monitoring algorithm. This attribute is user configurable and affects all sensors within the sensorgroup."
   "t_critical_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Lower Threshold of the SensorGroup."
   "t_critical_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Upper Threshold of the SensorGroup."
   "t_major_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Lower Threshold of the SensorGroup."
   "t_critical_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Upper Threshold of the SensorGroup."
   "t_minor_lower_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Lower Threshold of the SensorGroup."
   "t_minor_upper_group (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Upper Threshold of the SensorGroup."
   "unit_base_group (Optional)", "plain", "xsd:string", "The unit base of the sensor. e.g. revolutions, C (Celcius), etc."
   "unit_modifier_group (Optional)", "plain", "xsd:string", "The unit modifier of the sensor. e.g. 10^2 (\*100)."
   "unit_rate_group (Optional)", "plain", "xsd:string", "The unit rate of the sensor. e.g. /h (per hour)."
   "algorithm (Optional)", "plain", "xsd:string", "The system sensor algorithm version information."
   "actions_critical_group (Optional)", "plain", "xsd:string", "The actions to take upon critical threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "actions_major_group (Optional)", "plain", "xsd:string", "The actions to take upon major threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."
   "actions_minor_group (Optional)", "plain", "xsd:string", "The actions to take upon minor threshold sensor event. e.g. ``alarm, ignore``. This attribute is user configurable and affects all sensors within the sensorgroup."

::

   [
      {
         "path":"/actions_critical_group",
         "value":"alarm",
         "op":"replace"
      },
      {
         "path":"/actions_major_group",
         "value":"alarm",
         "op":"replace"
      },
      {
         "path":"/actions_minor_group",
         "value":"ignore",
         "op":"replace"
      }
   ]

::

   {
      "audit_interval_group":null,
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b",
            "rel":"bookmark"
         }
      ],
      "t_critical_upper_group":"120",
      "updated_at":"2015-09-08T13:37:15.547558+00:00",
      "isensors":[
         {
            "href":"http://192.168.204.2:6385/v1/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b/isensors",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/isensorgroups/58f297b6-7d32-409a-979b-e141ca50c39b/isensors",
            "rel":"bookmark"
         }
      ],
      "t_critical_lower_group":"-40",
      "t_minor_upper_group":"85",
      "t_minor_lower_group":"-10",
      "uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
      "unit_modifier_group":"1",
      "capabilities":{

      },
      "state":'enabled',
      "unit_rate_group":null,
      "actions_major_group":"alarm",
      "suppress":"False",
      "actions_minor_group":"ignore",
      "sensorgroupname":"cpuTemp",
      "sensors":null,
      "actions_configurable":null,
      "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
      "t_major_lower_group":"-20",
      "unit_base_group":"Celcius",
      "sensortype":"temperature",
      "algorithm":null,
      "datatype":"analog",
      "possible_states":null,
      "created_at":"2015-09-08T12:46:36.808611+00:00",
      "actions_critical_group":"alarm",
      "t_major_upper_group":"10"
   }

-------
Sensor
-------

These APIs allow the display of the status and operational state of
various sensor areas of a host. The modification of the certain sensor
attributes is also supported through these APIs. Examples of different
sensortypes are as defined for the sensorgroup.

****************************************
Lists the sensor information of a host
****************************************

.. rest_method:: GET /v1/ihosts/​{host_id}​/isensors

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_id", "URI", "csapi:UUID", "The unique identifier of an existing host."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "isensors (Optional)", "plain", "xsd:list", "The list of their associated sensor information for this host."
   "sensorgroup_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the sensorgroup."
   "suppress (Optional)", "plain", "xsd:boolean", "Specifies whether sensor actions are suppressed. ``True`` specifies suppressed. This attribute is user configurable."
   "sensorname (Optional)", "plain", "xsd:string", "The name of the sensor."
   "path (Optional)", "plain", "xsd:string", "The entity path of the sensor."
   "sensortype (Optional)", "plain", "xsd:string", "The sensortype of the sensor. e.g. ``temperature, voltage, fan, power``"
   "datatype (Optional)", "plain", "xsd:string", "The sensor datatype. e.g. ``discrete or analog``"
   "status (Optional)", "plain", "xsd:string", "The sensor status: One of ``ok, minor, major, critical``."
   "state (Optional)", "plain", "xsd:string", "The operational state of the sensor."
   "sensor_action_requested (Optional)", "plain", "xsd:string", "The sensor action requested for the sensor. Only applicable to action sensors."
   "t_critical_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Lower Threshold of the Sensor."
   "t_critical_upper (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Upper Threshold of the Sensor."
   "t_major_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Lower Threshold of the Sensor."
   "t_critical_upper (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Upper Threshold of the Sensor."
   "t_minor_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Lower Threshold of the Sensor."
   "t_minor_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Upper Threshold of the Sensor."
   "unit_base (Optional)", "plain", "xsd:string", "The unit base of the sensor. e.g. revolutions, C (Celcius), etc."
   "unit_modifier (Optional)", "plain", "xsd:string", "The unit modifier of the sensor. e.g. 10^2 (\*100)."
   "unit_rate (Optional)", "plain", "xsd:string", "The unit rate of the sensor. e.g. /h (per hour)."
   "algorithm (Optional)", "plain", "xsd:string", "The sensor algorithm version information."
   "actions_critical (Optional)", "plain", "xsd:string", "The actions to take upon critical threshold sensor event. e.g. ``alarm, ignore``"
   "actions_major (Optional)", "plain", "xsd:string", "The actions to take upon major threshold sensor event. e.g. ``alarm, ignore``"
   "actions_minor (Optional)", "plain", "xsd:string", "The actions to take upon minor threshold sensor event. e.g. ``alarm, ignore``"
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "isensors":[
         {
            "t_critical_upper":"120",
            "actions_minor":"ignore",
            "sensorname":"cpu0temp",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensors/18daed11-4c89-46ae-9197-a741e9c0bd2c",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensors/18daed11-4c89-46ae-9197-a741e9c0bd2c",
                  "rel":"bookmark"
               }
            ],
            "updated_at":"2015-09-08T13:37:36.366979+00:00",
            "path":null,
            "state_requested":null,
            "t_major_lower":"-20",
            "uuid":"18daed11-4c89-46ae-9197-a741e9c0bd2c",
            "t_minor_upper":"85",
            "capabilities":{

            },
            "actions_critical":"alarm",
            "state":"enabled",
            "sensorgroup_uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
            "t_major_upper":"105",
            "actions_major":"alarm",
            "status":"ok",
            "suppress":"False",
            "sensortype":"temperature",
            "t_critical_lower":"-40",
            "t_minor_lower":"-10",
            "unit_rate":null,
            "unit_modifier":"1",
            "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
            "unit_base":"Celcius",
            "algorithm":null,
            "datatype":"analog",
            "created_at":"2015-09-08T12:45:49.337205+00:00",
            "audit_interval":null
         },
         {
            "t_critical_upper":"120",
            "actions_minor":"ignore",
            "sensorname":"cpu1temp",
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensors/b6567aa3-f52f-44ef-8231-1df901f8c977",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensors/b6567aa3-f52f-44ef-8231-1df901f8c977",
                  "rel":"bookmark"
               }
            ],
            "updated_at":"2015-09-08T13:43:08.497704+00:00",
            "path":null,
            "state_requested":null,
            "t_major_lower":"-20",
            "uuid":"b6567aa3-f52f-44ef-8231-1df901f8c977",
            "t_minor_upper":"85",
            "capabilities":{

            },
            "actions_critical":"alarm",
            "state":"enabled",
            "sensorgroup_uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
            "t_major_upper":"105",
            "actions_major":"alarm",
            "status":"ok",
            "suppress":"False",
            "sensortype":"temperature",
            "t_critical_lower":"-40",
            "t_minor_lower":"-10",
            "unit_rate":null,
            "unit_modifier":"1",
            "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
            "unit_base":"Celcius",
            "algorithm":null,
            "datatype":"analog",
            "created_at":"2015-09-08T12:45:55.638149+00:00",
            "audit_interval":null
         },
         {
            "status":null,
            "actions_minor":null,
            "uuid":"d6258b09-5e2e-44d4-bec7-930d378e237c",
            "algorithm":null,
            "updated_at":null,
            "datatype":"discrete",
            "suppress":"False",
            "created_at":"2015-09-08T13:46:17.790654+00:00",
            "sensorgroup_uuid":null,
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensors/d6258b09-5e2e-44d4-bec7-930d378e237c",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensors/d6258b09-5e2e-44d4-bec7-930d378e237c",
                  "rel":"bookmark"
               }
            ],
            "capabilities":{

            },
            "actions_critical":null,
            "sensortype":"watchdog",
            "state":null,
            "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
            "state_requested":null,
            "path":null,
            "audit_interval":null,
            "actions_major":null,
            "sensorname":"cpuwdt"
         },
         {
            "status":"ok",
            "actions_minor":null,
            "uuid":"27f49183-cf73-434c-bff5-5e02ba6076fc",
            "algorithm":null,
            "updated_at":"2015-09-08T13:47:17.738427+00:00",
            "datatype":"discrete",
            "suppress":"False",
            "created_at":"2015-09-08T13:46:42.624861+00:00",
            "sensorgroup_uuid":null,
            "links":[
               {
                  "href":"http://192.168.204.2:6385/v1/isensors/27f49183-cf73-434c-bff5-5e02ba6076fc",
                  "rel":"self"
               },
               {
                  "href":"http://192.168.204.2:6385/isensors/27f49183-cf73-434c-bff5-5e02ba6076fc",
                  "rel":"bookmark"
               }
            ],
            "capabilities":{

            },
            "actions_critical":null,
            "sensortype":"watchdog",
            "state":"enabled",
            "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
            "state_requested":null,
            "path":null,
            "audit_interval":null,
            "actions_major":null,
            "sensorname":"fwwdt"
         }
      ]
   }

This operation does not accept a request body.

***************************************************
Shows the sensor information of a specific sensor
***************************************************

.. rest_method:: GET /v1/isensors/​{sensor_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensor_id", "URI", "csapi:UUID", "The unique identifier of a sensor."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensorgroup_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the sensorgroup."
   "suppress (Optional)", "plain", "xsd:boolean", "Specifies whether sensor actions are suppressed. ``True`` specifies suppressed. This attribute is user configurable."
   "sensorname (Optional)", "plain", "xsd:string", "The name of the sensor."
   "path (Optional)", "plain", "xsd:string", "The entity path of the sensor."
   "sensortype (Optional)", "plain", "xsd:string", "The sensortype of the sensor. e.g. ``temperature, voltage, fan, power``"
   "datatype (Optional)", "plain", "xsd:string", "The sensor datatype. e.g. ``discrete or analog``"
   "status (Optional)", "plain", "xsd:string", "The sensor status: One of ``ok, minor, major, critical``."
   "state (Optional)", "plain", "xsd:string", "The operational state of the sensor."
   "sensor_action_requested (Optional)", "plain", "xsd:string", "The sensor action requested for the sensor. Only applicable to action sensors."
   "t_critical_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Lower Threshold of the Sensor."
   "t_critical_upper (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Upper Threshold of the Sensor."
   "t_major_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Lower Threshold of the Sensor."
   "t_critical_upper (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Upper Threshold of the Sensor."
   "t_minor_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Lower Threshold of the Sensor."
   "t_minor_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Upper Threshold of the Sensor."
   "unit_base (Optional)", "plain", "xsd:string", "The unit base of the sensor. e.g. revolutions, C (Celcius), etc."
   "unit_modifier (Optional)", "plain", "xsd:string", "The unit modifier of the sensor. e.g. 10^2 (\*100)."
   "unit_rate (Optional)", "plain", "xsd:string", "The unit rate of the sensor. e.g. /h (per hour)."
   "algorithm (Optional)", "plain", "xsd:string", "The sensor algorithm version information."
   "actions_critical (Optional)", "plain", "xsd:string", "The actions to take upon critical threshold sensor event. e.g. ``alarm, ignore``"
   "actions_major (Optional)", "plain", "xsd:string", "The actions to take upon major threshold sensor event. e.g. ``alarm, ignore``"
   "actions_minor (Optional)", "plain", "xsd:string", "The actions to take upon minor threshold sensor event. e.g. ``alarm, ignore``"
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
      "t_critical_upper":"120",
      "actions_minor":"ignore",
      "sensorname":"cpu1temp",
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/isensors/b6567aa3-f52f-44ef-8231-1df901f8c977",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/isensors/b6567aa3-f52f-44ef-8231-1df901f8c977",
            "rel":"bookmark"
         }
      ],
      "updated_at":"2015-09-08T13:37:36.396138+00:00",
      "path":null,
      "state_requested":null,
      "t_major_lower":"-20",
      "uuid":"b6567aa3-f52f-44ef-8231-1df901f8c977",
      "t_minor_upper":"85",
      "capabilities":{ 

      },
      "actions_critical":"alarm",
      "state":"enabled",
      "sensorgroup_uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
      "t_major_upper":"105",
      "actions_major":"alarm",
      "status":"ok",
      "suppress":"False",
      "sensortype":"temperature",
      "t_critical_lower":"-40",
      "t_minor_lower":"-10",
      "unit_rate":null,
      "unit_modifier":"1",
      "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
      "unit_base":"Celcius",
      "algorithm":null,
      "datatype":"analog",
      "created_at":"2015-09-08T12:45:55.638149+00:00",
      "audit_interval":60
   }

This operation does not accept a request body.

******************************************************
Modifies the sensor information of a specific sensor
******************************************************

.. rest_method:: PATCH /v1/isensors/​{sensor_id}​

**Normal response codes**

200

**Error response codes**

badMediaType (415)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensor_id", "URI", "csapi:UUID", "The unique identifier of a sensor."
   "suppress (Optional)", "plain", "xsd:boolean", "If ``True``, suppresses any actions configured for the sensor. When suppressed, the sensor remains monitored, but the actions configured will be suppressed."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "sensorgroup_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the sensorgroup."
   "suppress (Optional)", "plain", "xsd:boolean", "Specifies whether sensor actions are suppressed. ``True`` specifies suppressed. This attribute is user configurable."
   "sensorname (Optional)", "plain", "xsd:string", "The name of the sensor."
   "path (Optional)", "plain", "xsd:string", "The entity path of the sensor."
   "sensortype (Optional)", "plain", "xsd:string", "The sensortype of the sensor. e.g. ``temperature, voltage, fan, power``"
   "datatype (Optional)", "plain", "xsd:string", "The sensor datatype. e.g. ``discrete or analog``"
   "status (Optional)", "plain", "xsd:string", "The sensor status: One of ``ok, minor, major, critical``."
   "state (Optional)", "plain", "xsd:string", "The operational state of the sensor."
   "sensor_action_requested (Optional)", "plain", "xsd:string", "The sensor action requested for the sensor. Only applicable to action sensors."
   "t_critical_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Lower Threshold of the Sensor."
   "t_critical_upper (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Critical Upper Threshold of the Sensor."
   "t_major_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Lower Threshold of the Sensor."
   "t_critical_upper (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Major Upper Threshold of the Sensor."
   "t_minor_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Lower Threshold of the Sensor."
   "t_minor_lower (Optional)", "plain", "xsd:string", "For datatype analog sensors: the Minor Upper Threshold of the Sensor."
   "unit_base (Optional)", "plain", "xsd:string", "The unit base of the sensor. e.g. revolutions, C (Celcius), etc."
   "unit_modifier (Optional)", "plain", "xsd:string", "The unit modifier of the sensor. e.g. 10^2 (\*100)."
   "unit_rate (Optional)", "plain", "xsd:string", "The unit rate of the sensor. e.g. /h (per hour)."
   "algorithm (Optional)", "plain", "xsd:string", "The sensor algorithm version information."
   "actions_critical (Optional)", "plain", "xsd:string", "The actions to take upon critical threshold sensor event. e.g. ``alarm, ignore``"
   "actions_major (Optional)", "plain", "xsd:string", "The actions to take upon major threshold sensor event. e.g. ``alarm, ignore``"
   "actions_minor (Optional)", "plain", "xsd:string", "The actions to take upon minor threshold sensor event. e.g. ``alarm, ignore``"

::

   [
      {
         "path":"/suppress",
         "value":"False",
         "op":"replace"
      }
   ]

::

   {
      "t_critical_upper":"120",
      "actions_minor":"ignore",
      "sensorname":"cpu1temp",
      "links":[
         {
            "href":"http://192.168.204.2:6385/v1/isensors/b6567aa3-f52f-44ef-8231-1df901f8c977",
            "rel":"self"
         },
         {
            "href":"http://192.168.204.2:6385/isensors/b6567aa3-f52f-44ef-8231-1df901f8c977",
            "rel":"bookmark"
         }
      ],
      "updated_at":"2015-09-08T13:42:55.448781+00:00",
      "path":null,
      "state_requested":null,
      "t_major_lower":"-20",
      "uuid":"b6567aa3-f52f-44ef-8231-1df901f8c977",
      "t_minor_upper":"85",
      "capabilities":{

      },
      "actions_critical":"alarm",
      "state":"enabled",
      "sensorgroup_uuid":"58f297b6-7d32-409a-979b-e141ca50c39b",
      "t_major_upper":"105",
      "actions_major":"alarm",
      "status":"ok",
      "suppress":"False",
      "sensortype":"temperature",
      "t_critical_lower":"-40",
      "t_minor_lower":"-10",
      "unit_rate":null,
      "unit_modifier":"1",
      "host_uuid":"0a66f89c-412c-4480-a38d-fdad248467a3",
      "unit_base":"Celcius",
      "algorithm":null,
      "datatype":"analog",
      "created_at":"2015-09-08T12:45:55.638149+00:00",
      "audit_interval":60
   }


------------
LLDP Agents
------------

These APIs allow the display of the lldp agents of a host and their
attributes.

********************************
List the LLDP agents of a host
********************************

.. rest_method:: GET /v1/lldp_agents

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "lldp_agents (Optional)", "plain", "xsd:list", "The list of LLDP agents of a host."
   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host containing the lldp agent."
   "local_port (Optional)", "plain", "xsd:string", "The name of the local port to which this lldp agent belongs."
   "chassis_id (Optional)", "plain", "xsd:string", "The 802.1AB chassis identifier advertised by the lldp agent."
   "port_identifier (Optional)", "plain", "xsd:string", "The 802.1AB port identifier advertised by the lldp agent."
   "port_description (Optional)", "plain", "xsd:string", "The 802.1AB port descrioption advertised by the lldp agent."
   "ttl (Optional)", "plain", "xsd:string", "The 802.1AB time to live advertised by the lldp agent."
   "system_description (Optional)", "plain", "xsd:string", "The 802.1AB system description advertised by the lldp agent."
   "system_name (Optional)", "plain", "xsd:string", "The 802.1AB system name advertised by the lldp agent."
   "system_capabilities (Optional)", "plain", "xsd:string", "The 802.1AB system capabilities advertised by the lldp agent."
   "management_address (Optional)", "plain", "xsd:string", "The 802.1AB management address advertised by the lldp agent."
   "dot1_lag (Optional)", "plain", "xsd:string", "The 802.1AB link aggregation status advertised by the lldp agent."
   "dot1_vlan_names (Optional)", "plain", "xsd:string", "The 802.1AB vlan names advertised by the lldp agent."
   "dot3_mac_status (Optional)", "plain", "xsd:string", "The 802.1AB MAC status advertised by the lldp agent."
   "dot3_max_frame (Optional)", "plain", "xsd:string", "The 802.1AB maximum frame size advertised by the lldp agent."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
     "lldp_agents": [
       {
         "status": "rx=enabled,tx=enabled",
         "system_name": "controller-0:vbox",
         "port_description": "eth0",
         "dot3_max_frame": null,
         "port_uuid": "6d222be7-221d-4556-be69-145a21afeb69",
         "uuid": "a4a734d1-b749-412e-ad32-ea511d8e7499",
         "links": [
           {
             "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_agents\/a4a734d1-b749-412e-ad32-ea511d8e7499",
             "rel": "self"
           },
           {
             "href": "http:\/\/192.168.204.2:6385\/lldp_agents\/a4a734d1-b749-412e-ad32-ea511d8e7499",
             "rel": "bookmark"
           }
         ],
         "dot3_mac_status": null,
         "port_identifier": "08:00:27:19:ad:0d",
         "system_description": "StarlingX",
         "updated_at": null,
         "chassis_id": "08:00:27:19:ad:0d",
         "dot1_lag": "capable=y,enabled=n",
         "port_name": "eth0",
         "port_namedisplay": null,
         "management_address": "10.10.10.3, fe80::a00:27ff:fe19:ad0d",
         "ttl": "120",
         "dot1_vlan_names": null,
         "created_at": "2016-04-11T15:40:18.366046+00:00",
         "host_uuid": "9285be64-83a9-4067-905f-ab50b5f48823",
         "system_capabilities": "station"
       },
       {
         "status": "rx=enabled,tx=enabled",
         "system_name": "controller-0:vbox",
         "port_description": "eth1",
         "dot3_max_frame": null,
         "port_uuid": "c1e44562-fe58-4255-ab2f-d58aa5be1ced",
         "uuid": "0ec02f88-623f-4148-801c-08d2c4c015f4",
         "links": [
           {
             "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_agents\/0ec02f88-623f-4148-801c-08d2c4c015f4",
             "rel": "self"
           },
           {
             "href": "http:\/\/192.168.204.2:6385\/lldp_agents\/0ec02f88-623f-4148-801c-08d2c4c015f4",
             "rel": "bookmark"
           }
         ],
         "dot3_mac_status": null,
         "port_identifier": "08:00:27:b9:af:30",
         "system_description": "StarlingX",
         "updated_at": null,
         "chassis_id": "08:00:27:19:ad:0d",
         "dot1_lag": "capable=y,enabled=n",
         "port_name": "eth1",
         "port_namedisplay": null,
         "management_address": "10.10.10.3, fe80::a00:27ff:fe19:ad0d",
         "ttl": "120",
         "dot1_vlan_names": null,
         "created_at": "2016-04-11T15:40:18.396903+00:00",
         "host_uuid": "9285be64-83a9-4067-905f-ab50b5f48823",
         "system_capabilities": "station"
       },
       {
         "status": "rx=enabled,tx=enabled",
         "system_name": "controller-0:vbox",
         "port_description": "eth2",
         "dot3_max_frame": null,
         "port_uuid": "3b525a1d-9aad-4e74-9d51-8be28db9c4d6",
         "uuid": "0c3e3cca-de7d-4b6a-9766-61f16ed34e78",
         "links": [
           {
             "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_agents\/0c3e3cca-de7d-4b6a-9766-61f16ed34e78",
             "rel": "self"
           },
           {
             "href": "http:\/\/192.168.204.2:6385\/lldp_agents\/0c3e3cca-de7d-4b6a-9766-61f16ed34e78",
             "rel": "bookmark"
           }
         ],
         "dot3_mac_status": null,
         "port_identifier": "08:00:27:b7:c9:78",
         "system_description": "StarlingX",
         "updated_at": null,
         "chassis_id": "08:00:27:19:ad:0d",
         "dot1_lag": "capable=y,enabled=n",
         "port_name": "eth2",
         "port_namedisplay": null,
         "management_address": "10.10.10.3, fe80::a00:27ff:fe19:ad0d",
         "ttl": "120",
         "dot1_vlan_names": null,
         "created_at": "2016-04-11T15:40:18.424135+00:00",
         "host_uuid": "9285be64-83a9-4067-905f-ab50b5f48823",
         "system_capabilities": "station"
       }
     ]
   }

This operation does not accept a request body.

***********************************************
Shows the attributes of a specific LLDP agent
***********************************************

.. rest_method:: GET /v1/lldp_agents/​{lldp_agent_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "lldp_agent_id", "URI", "csapi:UUID", "The unique identifier of an existing lldp agent."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host containing the lldp agent."
   "local_port (Optional)", "plain", "xsd:string", "The name of the local port to which this lldp agent belongs."
   "chassis_id (Optional)", "plain", "xsd:string", "The 802.1AB chassis identifier advertised by the lldp agent."
   "port_identifier (Optional)", "plain", "xsd:string", "The 802.1AB port identifier advertised by the lldp agent."
   "port_description (Optional)", "plain", "xsd:string", "The 802.1AB port descrioption advertised by the lldp agent."
   "ttl (Optional)", "plain", "xsd:string", "The 802.1AB time to live advertised by the lldp agent."
   "system_description (Optional)", "plain", "xsd:string", "The 802.1AB system description advertised by the lldp agent."
   "system_name (Optional)", "plain", "xsd:string", "The 802.1AB system name advertised by the lldp agent."
   "system_capabilities (Optional)", "plain", "xsd:string", "The 802.1AB system capabilities advertised by the lldp agent."
   "management_address (Optional)", "plain", "xsd:string", "The 802.1AB management address advertised by the lldp agent."
   "dot1_lag (Optional)", "plain", "xsd:string", "The 802.1AB link aggregation status advertised by the lldp agent."
   "dot1_vlan_names (Optional)", "plain", "xsd:string", "The 802.1AB vlan names advertised by the lldp agent."
   "dot3_mac_status (Optional)", "plain", "xsd:string", "The 802.1AB MAC status advertised by the lldp agent."
   "dot3_max_frame (Optional)", "plain", "xsd:string", "The 802.1AB maximum frame size advertised by the lldp agent."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
     "port_description": "eth0",
     "port_uuid": "6d222be7-221d-4556-be69-145a21afeb69",
     "links": [
       {
         "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_agents\/a4a734d1-b749-412e-ad32-ea511d8e7499",
         "rel": "self"
       },
       {
         "href": "http:\/\/192.168.204.2:6385\/lldp_agents\/a4a734d1-b749-412e-ad32-ea511d8e7499",
         "rel": "bookmark"
       }
     ],
     "port_identifier": "08:00:27:19:ad:0d",
     "updated_at": null,
     "port_name": "eth0",
     "port_namedisplay": null,
     "ttl": "120",
     "dot1_vlan_names": null,
     "uuid": "a4a734d1-b749-412e-ad32-ea511d8e7499",
     "system_description": "StarlingX",
     "chassis_id": "08:00:27:19:ad:0d",
     "dot1_lag": "capable=y,enabled=n",
     "system_capabilities": "station",
     "status": "rx=enabled,tx=enabled",
     "tlvs": [
       {
         "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_agents\/a4a734d1-b749-412e-ad32-ea511d8e7499\/tlvs",
         "rel": "self"
       },
       {
         "href": "http:\/\/192.168.204.2:6385\/lldp_agents\/a4a734d1-b749-412e-ad32-ea511d8e7499\/tlvs",
         "rel": "bookmark"
       }
     ],
     "host_uuid": "9285be64-83a9-4067-905f-ab50b5f48823",
     "system_name": "controller-0:vbox",
     "dot3_max_frame": null,
     "dot3_mac_status": null,
     "created_at": "2016-04-11T15:40:18.366046+00:00",
     "management_address": "10.10.10.3, fe80::a00:27ff:fe19:ad0d"
   }

This operation does not accept a request body.

---------------
LLDP Neighbors
---------------

These APIs allow the display of the lldp neighbors of a host and their
attributes.

***********************************
List the LLDP neighbors of a host
***********************************

.. rest_method:: GET /v1/lldp_neighbours

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "lldp_agents (Optional)", "plain", "xsd:list", "The list of LLDP neighbors of a host."
   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host containing the lldp neighbor."
   "msap (Optional)", "plain", "xsd:string", "The MAC service access point identifier of the lldp neighbor."
   "local_port (Optional)", "plain", "xsd:string", "The name of the local port to which this lldp neighbor is connected."
   "chassis_id (Optional)", "plain", "xsd:string", "The 802.1AB chassis identifier advertised by the lldp neighbor."
   "port_identifier (Optional)", "plain", "xsd:string", "The 802.1AB port identifier advertised by the lldp neighbor."
   "port_description (Optional)", "plain", "xsd:string", "The 802.1AB port descrioption advertised by the lldp neighbor."
   "ttl (Optional)", "plain", "xsd:string", "The 802.1AB time to live advertised by the lldp neighbor."
   "system_description (Optional)", "plain", "xsd:string", "The 802.1AB system description advertised by the lldp neighbor."
   "system_name (Optional)", "plain", "xsd:string", "The 802.1AB system name advertised by the lldp neighbor."
   "system_capabilities (Optional)", "plain", "xsd:string", "The 802.1AB system capabilities advertised by the lldp neighbor."
   "management_address (Optional)", "plain", "xsd:string", "The 802.1AB management address advertised by the lldp neighbor."
   "dot1_lag (Optional)", "plain", "xsd:string", "The 802.1AB link aggregation status advertised by the lldp neighbor."
   "dot1_vlan_names (Optional)", "plain", "xsd:string", "The 802.1AB vlan names advertised by the lldp neighbor."
   "dot1_port_vid (Optional)", "plain", "xsd:string", "The 802.1AB port vlan id advertised by the lldp neighbor."
   "dot1_proto_vids (Optional)", "plain", "xsd:string", "The 802.1AB protocol vlan ids advertised by the lldp neighbor."
   "dot1_proto_ids (Optional)", "plain", "xsd:string", "The 802.1AB protocol ids advertised by the lldp neighbor."
   "dot3_mac_status (Optional)", "plain", "xsd:string", "The 802.1AB MAC status advertised by the lldp neighbor."
   "dot3_max_frame (Optional)", "plain", "xsd:string", "The 802.1AB maximum frame size advertised by the lldp neighbor."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
     "lldp_neighbours": [
       {
         "port_description": null,
         "msap": "08:00:27:ae:0a:60,Management1",
         "port_uuid": "6d222be7-221d-4556-be69-145a21afeb69",
         "links": [
           {
             "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_neighbours\/5742ce56-0420-42df-9096-d13b0a118521",
             "rel": "self"
           },
           {
             "href": "http:\/\/192.168.204.2:6385\/lldp_neighbours\/5742ce56-0420-42df-9096-d13b0a118521",
             "rel": "bookmark"
           }
         ],
         "port_identifier": "Management1",
         "updated_at": null,
         "dot1_vid_digest": null,
         "port_name": "eth0",
         "port_namedisplay": null,
         "ttl": "104",
         "dot1_port_vid": null,
         "dot1_vlan_names": null,
         "uuid": "5742ce56-0420-42df-9096-d13b0a118521",
         "system_description": "Arista Networks EOS version 4.15.0F running on an Arista Networks vEOS",
         "dot1_management_vid": null,
         "chassis_id": "08:00:27:ae:0a:60",
         "dot1_lag": "capable=y,enabled=n",
         "dot1_proto_vids": null,
         "system_capabilities": "bridge",
         "dot1_proto_ids": null,
         "host_uuid": "9285be64-83a9-4067-905f-ab50b5f48823",
         "system_name": "arista-swtich",
         "dot3_power_mdi": null,
         "dot3_max_frame": "1518",
         "dot3_mac_status": null,
         "created_at": "2016-04-11T15:40:17.982210+00:00",
         "management_address": "10.10.10.253"
       }
     ]
   }

This operation does not accept a request body.

**************************************************
Shows the attributes of a specific LLDP neighbor
**************************************************

.. rest_method:: GET /v1/lldp_neighbours/​{lldp_neighbor_id}​

**Normal response codes**

200

**Error response codes**

computeFault (400, 500, ...), serviceUnavailable (503), badRequest (400),
unauthorized (401), forbidden (403), badMethod (405), overLimit (413),
itemNotFound (404)

**Request parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "lldp_neighbor_id", "URI", "csapi:UUID", "The unique identifier of an existing lldp neighbor."

**Response parameters**

.. csv-table::
   :header: "Parameter", "Style", "Type", "Description"
   :widths: 20, 20, 20, 60

   "host_uuid (Optional)", "plain", "csapi:UUID", "The UUID of the host containing the lldp neighbor."
   "msap (Optional)", "plain", "xsd:string", "The MAC service access point identifier of the lldp neighbor."
   "local_port (Optional)", "plain", "xsd:string", "The name of the local port to which this lldp neighbor is connected."
   "chassis_id (Optional)", "plain", "xsd:string", "The 802.1AB chassis identifier advertised by the lldp neighbor."
   "port_identifier (Optional)", "plain", "xsd:string", "The 802.1AB port identifier advertised by the lldp neighbor."
   "port_description (Optional)", "plain", "xsd:string", "The 802.1AB port descrioption advertised by the lldp neighbor."
   "ttl (Optional)", "plain", "xsd:string", "The 802.1AB time to live advertised by the lldp neighbor."
   "system_description (Optional)", "plain", "xsd:string", "The 802.1AB system description advertised by the lldp neighbor."
   "system_name (Optional)", "plain", "xsd:string", "The 802.1AB system name advertised by the lldp neighbor."
   "system_capabilities (Optional)", "plain", "xsd:string", "The 802.1AB system capabilities advertised by the lldp neighbor."
   "management_address (Optional)", "plain", "xsd:string", "The 802.1AB management address advertised by the lldp neighbor."
   "dot1_lag (Optional)", "plain", "xsd:string", "The 802.1AB link aggregation status advertised by the lldp neighbor."
   "dot1_vlan_names (Optional)", "plain", "xsd:string", "The 802.1AB vlan names advertised by the lldp neighbor."
   "dot1_port_vid (Optional)", "plain", "xsd:string", "The 802.1AB port vlan id advertised by the lldp neighbor."
   "dot1_proto_vids (Optional)", "plain", "xsd:string", "The 802.1AB protocol vlan ids advertised by the lldp neighbor."
   "dot1_proto_ids (Optional)", "plain", "xsd:string", "The 802.1AB protocol ids advertised by the lldp neighbor."
   "dot3_mac_status (Optional)", "plain", "xsd:string", "The 802.1AB MAC status advertised by the lldp neighbor."
   "dot3_max_frame (Optional)", "plain", "xsd:string", "The 802.1AB maximum frame size advertised by the lldp neighbor."
   "uuid (Optional)", "plain", "csapi:UUID", "The universally unique identifier for this object."
   "links (Optional)", "plain", "xsd:list", "For convenience, resources contain links to themselves. This allows a client to easily obtain rather than construct resource URIs. The following types of link relations are associated with resources: a self link containing a versioned link to the resource, and a bookmark link containing a permanent link to a resource that is appropriate for long term storage."
   "created_at (Optional)", "plain", "xsd:dateTime", "The time when the object was created."
   "updated_at (Optional)", "plain", "xsd:dateTime", "The time when the object was last updated."

::

   {
     "port_description": null,
     "msap": "08:00:27:ae:0a:60,Management1",
     "port_uuid": "6d222be7-221d-4556-be69-145a21afeb69",
     "links": [
       {
         "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_neighbours\/5742ce56-0420-42df-9096-d13b0a118521",
         "rel": "self"
       },
       {
         "href": "http:\/\/192.168.204.2:6385\/lldp_neighbours\/5742ce56-0420-42df-9096-d13b0a118521",
         "rel": "bookmark"
       }
     ],
     "port_identifier": "Management1",
     "updated_at": null,
     "dot1_vid_digest": null,
     "port_name": "eth0",
     "port_namedisplay": null,
     "ttl": "104",
     "dot1_port_vid": null,
     "dot1_vlan_names": null,
     "uuid": "5742ce56-0420-42df-9096-d13b0a118521",
     "system_description": "Arista Networks EOS version 4.15.0F running on an Arista Networks vEOS",
     "dot1_management_vid": null,
     "chassis_id": "08:00:27:ae:0a:60",
     "dot1_lag": "capable=y,enabled=n",
     "dot1_proto_vids": null,
     "system_capabilities": "bridge",
     "tlvs": [
       {
         "href": "http:\/\/192.168.204.2:6385\/v1\/lldp_neighbours\/5742ce56-0420-42df-9096-d13b0a118521\/tlvs",
         "rel": "self"
       },
       {
         "href": "http:\/\/192.168.204.2:6385\/lldp_neighbours\/5742ce56-0420-42df-9096-d13b0a118521\/tlvs",
         "rel": "bookmark"
       }
     ],
     "dot1_proto_ids": null,
     "host_uuid": "9285be64-83a9-4067-905f-ab50b5f48823",
     "system_name": "arista-swtich",
     "dot3_power_mdi": null,
     "dot3_max_frame": "1518",
     "dot3_mac_status": null,
     "created_at": "2016-04-11T15:40:17.982210+00:00",
     "management_address": "10.10.10.253"
   }

This operation does not accept a request body.











