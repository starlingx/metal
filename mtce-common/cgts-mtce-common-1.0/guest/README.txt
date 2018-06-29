This maintenance common guest service was introduced in cgts release 2 in order to
provide a means of guest heartbeat orchistration under VIM (system management) control.

This module includes header files exported to the sysroot by the original cgts 
release 1 heartbeat service for messaging structure definition.

That original module builds a common heartbeat daemon capable of running on the
controller, compute and within the vm. The controller version did nothing except
for respond to heartbeats from the compute version. The compute instance was the
brains of the heartbeat service reponsible for starting heartbeat to the VMs and
interfacing with Nova to take actions.

The introduction of this module changes a large part of that.
Here is a summary of the changes

1. Packaging and Initialization:

   The source code for the original cgts release 1 heartbeat daemon remains
   unchanged but the packaging of the heartbeat daemon and heartbeat_init
   script are modified

   a. image.inc and filter_out packaging files are modified to exclude the
      heartbeat daemon from being packaged on the controller.

   b. because the heartbeat daemon is still packaged on the compute
      heartbeat_init script is modified to prevent the heartbeat 
      daemon from being spawned on the compute host.

   Note: the heartbeat daemon continues to be packaged and spawned in the guest
         as it did in release 1 - no change there.

2. Compute Function: Heartbeats the guest and reports failures.
   
   Package: cgts-mtce-common-guestServer-1.0-r54.0.x86_64.rpm
   Binary: /usr/local/bin/guestServer
   Init: /etc/init.d/guestServer
   Managed: /etc/pmon.d/guestServer.pmon

3. Controller Function: Guest heartbeat control and event proxy

   Package: cgts-mtce-common-guestAgent-x.x-rxx.x86_64.rpm
   Binary: /usr/local/bin/guestAgent
   Init: /usr/lib/ocf/resource.d/platform/guestAgent
   Managed: by SM
   
   The heartbeat daemon that did run on the controller is replaced by a new
   guestAgent daemon performing the following functions

   a. HTTP Command Receiver : to which the VIM sends instance control commands.
   b. HTTP Event Transmitter: to which the daemon can send instance failure
                              events and state query commands to the VIM.
   c. State query audit to the 

Behavioral Executive Summary:

The guestServer daemon (on the compute) listens for (using inotify) 'uuid'
UNIX named heartbeat communication channels that nova:libvirt creates and
opens in /var/lib/libvirt/qemu whenever an instance is created. Example: 

/var/lib/libvirt/qemu/cgcs.heartbeat.02e172a9-aeae-4cef-a6bc-7eb9de7825d6.sock

The guestServer connects (and auto reconnects) to these channels when they are
created or modified and disconnects from them when deleted.

Once connected, the guestServer listens for TCP messages over that UNIX named
socket. 

If a guest supports heartbeating then it will run the heartbeat_init script
during its initialization process. Once the heartbeat daemon is running it 
will periodically send init request messages to the libvirt pipe that, 
on the host side, the guestServer is listening for. 

on receipt of an init message, the guestServer will extract name, timeout
and corrective action info from it and then send back an 'init_ack' followed
by a continuous heartbeat cycle consisting of sending a 'challenge' request
messages and expecting a correct computational responses within a guest specified
heartbeat window. Failure to comply will result in a corrective action that was
specified in the init message from the guest.

The VIM is responsible for enabling and disabling heartbeat fault reporting as
well as taking the guest specified corrective action in he event of a heartbeat
failure.

The VIM guestAgent running 
