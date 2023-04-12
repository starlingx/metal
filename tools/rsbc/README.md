The process of enabling/disabling Secure Boot and uploading a certificate on a
server is tedious, complicated, time consuming and potentially problematic.

The Redfish Secure Boot Manager Tool uses the Redfish Protocol to automate the
process of enabling/disabling Secure Boot and uploading certificates to a host.
The tool also supports a service option which allows the user to query which
Redfish services are supported on the server(s).

The user specifies which server(s) they would like to modify using the
--config flag, which supports multiple servers, or the --bmc_ip, --bmc_un,
and --bmc_pw flags, which support one specific server. The user should
supply the path to the .yaml configuration file when using the --config
flag and the ip address of the server, username, and the password when using
the --bmc_ip, --bmc_un, and --bmc_pw flags.

There are four modes to the tool:

--query checks if Secure Boot is supported on the server. It then returns the
state of Secure Boot and outputs a list of Secure Boot certificates

--service returns which Redfish services are supported on the server(s)

--enable and --disable enables or disables Secure boot on the server(s)

--upload Uploads a .pem or .der certificate to the server's Secure Boot database

Examples of usage with --config:
./rsbc.py --query                     --config ./query_server.yaml
./rsbc.py --enable                    --config ./sb_server.yaml
./rsbc.py --disable                   --config ./sb_server.yaml
./rsbc.py --upload ./certs/TiBoot.crt --config ./sb_server.yaml

Examples of usage with --bmc_ip and --bmc_pw:
./rsbc.py --query   --bmc_ip <BMC IP address> --bmc_un <BMC username> --bmc_pw <BMC password>>
./rsbc.py --enable  --bmc_ip <BMC IP address> --bmc_un <BMC username> --bmc_pw <BMC password>>
./rsbc.py --disable --bmc_ip <BMC IP address> --bmc_un <BMC username> --bmc_pw <BMC password>>
./rsbc.py --upload  --bmc_ip <BMC IP address> --bmc_un <BMC username> --bmc_pw <BMC password>>

Example of the format of a configuration file:

virtual_media_iso:
    yow2-xr11-025:
       bmc_username: <BMC username>
       bmc_address: <BMC IP address>
       bmc_password: <BMC password>

For more information, please see the Documentation of this service located at: https://confluence.wrs.com/display/CE/Redfish+Secure+Boot+Manager+Tool+HLD
