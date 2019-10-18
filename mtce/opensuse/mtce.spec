Summary: StarlingX Common Maintenance Package
Name: mtce
Version: 1.0.0
Release: 2
License: Apache-2.0
Group: System/Base
URL: https://www.starlingx.io

Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc-c++
BuildRequires: libssh2-1
BuildRequires: libssh2-devel
BuildRequires: libjson-c3
BuildRequires: libjson-c-devel
BuildRequires: fm-common
BuildRequires: fm-common-devel
BuildRequires: openssl
BuildRequires: openssl-devel
BuildRequires: libevent
BuildRequires: libevent-devel
BuildRequires: fm-mgr
BuildRequires: expect
BuildRequires: postgresql
BuildRequires: libuuid-devel
BuildRequires: systemd-devel
BuildRequires: cppcheck
BuildRequires: mtce-common-devel >= 1.0.0

Requires: util-linux
Requires: systemd
Requires: dpkg
Requires: time
Requires: expect
Requires: python-rtslib-fb
Requires: ipmitool

%description
StarlingX Host Maintenance services. A suite of daemons that provide
host maintainability and a high level of fault detection with automatic
notification and recovery.The Maintenance Service (mtcAgent/mtcClient)
manages hosts according to an abbreviated version of the CCITT X.731 ITU
specification. The Heartbeat Service (hbsAgent/hbsClient) adds fast failure
detection over the management and infstructure networks. The Process
Monitor service (pmond) add both passive and active process monitoring and
automatic recovery of stopped or killed processes. The File System Monitor
Service (fsmond) adds detection and reporting of local file system
problems. The Hardware Monitor Service (hwmond) adds present and predictive
hardware failure detection, reporting and recovery.
The Host Watchdog (hostwd) daemon watches for errors in
pmond and logs system information on error. All of these maintenance
services improve MTTD of node failures as well as resource overload and out
of spec operating conditions that can reduce outage time through automated
notification and recovery thereby improving overall platform availability
for the customer.

%package -n mtce-devel
Summary: StarlingX Server Maintenance Software Development Package
Group: Development/Libraries/Other
Provides: mtce-devel = %{version}-%{release}

%description -n mtce-devel
StarlingX Maintenance. This package contains header files,
and related items necessary for software development.

%package -n libamon1
Summary: StarlingX Server Maintenance Process Monitor Package
Group: System/Base
BuildRequires: cppcheck
Requires: util-linux
Requires: systemd
Requires: dpkg
Requires: time

%description -n libamon1
StarlingX Maintenance Process Monitor service (pmond) with
passive (pid), active (msg) and status (qry) process monitoring with
automatic recovery and failure reporting of registered failed processes.

%package -n libamon1-devel
Summary: StarlingX Server Maintenance Process Monitor Package devel
Group: Development/Libraries/Other
Requires: libamon1

%description -n libamon1-devel
Development library for the Maintenance Process Monitor serviced.

%package -n mtce-hwmon
Summary: StarlingX Server Maintenance Hardware Monitor Package
Group: System/Base
Requires: dpkg
Requires: time
Requires: ipmitool

%description -n mtce-hwmon
StarlingX Host Maintenance Hardware Monitor Service (hwmond) adds
in and out of service hardware sensor monitoring, alarming and recovery
handling.

%package -n mtce-hostw
Summary: StarlingX Server Common Maintenance Package
Group: System/Base
Requires: util-linux
Requires: systemd
Requires: dpkg
Requires: ipmitool

%description -n mtce-hostw
Titanium Cloud Host Maintenance services. A suite of daemons that provide
host maintainability and a high level of fault detection with automatic
notification and recovery.The Maintenance Service (mtcAgent/mtcClient)
manages hosts according to an abbreviated version of the CCITT X.731 ITU
specification. The Heartbeat Service (hbsAgent/hbsClient) adds fast failure
detection over the management and infstructure networks. The Process
Monitor service (pmond) add both passive and active process monitoring and
automatic recovery of stopped or killed processes. The File System Monitor
Service (fsmond) adds detection and reporting of local file system
problems. The Hardware Monitor Service (hwmond) adds present and predictive
hardware failure detection, reporting and recovery. The Guest Services
(guestAgent/guestServer) daemons control access into and heartbeat of guest
VMs on the worker. The Host Watchdog (hostwd) daemon watches for errors in
pmond and logs system information on error. All of these maintenance
services improve MTTD of node failures as well as resource overload and out
of spec operating conditions that can reduce outage time through automated
notification and recovery thereby improving overall platform availability
for the customer.

%package -n mtce-lmon
Summary: StarlingX Server Maintenance Link Monitor Package
Group: System/Base
BuildRequires: cppcheck
Requires: util-linux
Requires: systemd
Requires: dpkg
Requires: time

%description -n mtce-lmon
StarlingX Maintenance Link Monitor service (lmond) provides
netlink monitoring for provisioned oam, mgmt and cluster-host interfaces.

%define local_dir /usr/local
%define local_bindir %{local_dir}/bin
%define local_sbindir %{local_dir}/sbin
%define local_etc_pmond      %{_sysconfdir}/pmon.d
%define local_etc_goenabledd %{_sysconfdir}/goenabled.d
%define local_etc_servicesd  %{_sysconfdir}/services.d
%define local_etc_logrotated %{_sysconfdir}/logrotate.d
%define bmc_profilesd        %{_sysconfdir}/bmc/server_profiles.d
%define ocf_resourced %{_libdir}/ocf/resource.d

%prep
%setup -n %{name}-%{version}/src

# Build mtce package
%build
VER=%{version}
MAJOR=$(echo $VER | awk -F . '{print $1}')
MINOR=$(echo $VER | awk -F . '{print $2}')
make MAJOR=$MAJOR MINOR=$MINOR %{?_smp_mflags} build

%global _buildsubdir %{_builddir}/%{name}-%{version}/src

# Install mtce package
%install

VER=%{version}
MAJOR=$(echo $VER | awk -F . '{print $1}')
MINOR=$(echo $VER | awk -F . '{print $2}')

install -m 755 -d %{buildroot}%{_sysconfdir}
install -m 755 -d %{buildroot}/usr
install -m 755 -d %{buildroot}/%{_bindir}
install -m 755 -d %{buildroot}/usr/local
install -m 755 -d %{buildroot}%{local_bindir}
install -m 755 -d %{buildroot}/usr/local/sbin
install -m 755 -d %{buildroot}/%{_sbindir}
install -m 755 -d %{buildroot}%{_sysconfdir}/mtc
install -m 755 -d %{buildroot}%{_sysconfdir}/mtc/tmp

# Resource Agent Stuff
install -m 755 -d %{buildroot}%{_libdir}
install -m 755 -d %{buildroot}%{_libdir}/ocf
install -m 755 -d %{buildroot}%{_libdir}/ocf/resource.d
install -m 755 -d %{buildroot}%{_libdir}/ocf/resource.d/platform
install -m 755 -p -D %{_buildsubdir}/scripts/mtcAgent %{buildroot}%{_libdir}/ocf/resource.d/platform/mtcAgent
install -m 755 -p -D %{_buildsubdir}/hwmon/scripts/ocf/hwmon %{buildroot}%{_libdir}/ocf/resource.d/platform/hwmon

# config files
install -m 644 -p -D %{_buildsubdir}/scripts/mtc.ini %{buildroot}%{_sysconfdir}/mtc.ini
install -m 644 -p -D %{_buildsubdir}/scripts/mtc.conf %{buildroot}%{_sysconfdir}/mtc.conf
install -m 644 -p -D %{_buildsubdir}/fsmon/scripts/fsmond.conf %{buildroot}%{_sysconfdir}/mtc/fsmond.conf
install -m 644 -p -D %{_buildsubdir}/hwmon/scripts/hwmond.conf %{buildroot}%{_sysconfdir}/mtc/hwmond.conf
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/pmond.conf %{buildroot}%{_sysconfdir}/mtc/pmond.conf
install -m 644 -p -D %{_buildsubdir}/lmon/scripts/lmond.conf %{buildroot}%{_sysconfdir}/mtc/lmond.conf
install -m 644 -p -D %{_buildsubdir}/hostw/scripts/hostwd.conf %{buildroot}%{_sysconfdir}/mtc/hostwd.conf

install -m 755 -d %{buildroot}/%{_sysconfdir}/etc/bmc/server_profiles.d
install -m 644 -p -D %{_buildsubdir}/scripts/sensor_hp360_v1_ilo_v4.profile %{buildroot}/%{_sysconfdir}/bmc/server_profiles.d/sensor_hp360_v1_ilo_v4.profile
install -m 644 -p -D %{_buildsubdir}/scripts/sensor_hp380_v1_ilo_v4.profile %{buildroot}/%{_sysconfdir}/bmc/server_profiles.d/sensor_hp380_v1_ilo_v4.profile
install -m 644 -p -D %{_buildsubdir}/scripts/sensor_quanta_v1_ilo_v4.profile %{buildroot}/%{_sysconfdir}/bmc/server_profiles.d/sensor_quanta_v1_ilo_v4.profile

# binaries
install -m 755 -p -D %{_buildsubdir}/maintenance/mtcAgent %{buildroot}/%{local_bindir}/mtcAgent
install -m 755 -p -D %{_buildsubdir}/maintenance/mtcClient %{buildroot}/%{local_bindir}/mtcClient
install -m 755 -p -D %{_buildsubdir}/heartbeat/hbsAgent %{buildroot}/%{local_bindir}/hbsAgent
install -m 755 -p -D %{_buildsubdir}/heartbeat/hbsClient %{buildroot}/%{local_bindir}/hbsClient
install -m 755 -p -D %{_buildsubdir}/pmon/pmond %{buildroot}/%{local_bindir}/pmond
install -m 755 -p -D %{_buildsubdir}/lmon/lmond %{buildroot}/%{local_bindir}/lmond
install -m 755 -p -D %{_buildsubdir}/hostw/hostwd %{buildroot}/%{local_bindir}/hostwd
install -m 755 -p -D %{_buildsubdir}/fsmon/fsmond %{buildroot}/%{local_bindir}/fsmond
install -m 755 -p -D %{_buildsubdir}/hwmon/hwmond %{buildroot}/%{local_bindir}/hwmond
install -m 755 -p -D %{_buildsubdir}/mtclog/mtclogd %{buildroot}/%{local_bindir}/mtclogd
install -m 755 -p -D %{_buildsubdir}/alarm/mtcalarmd %{buildroot}/%{local_bindir}/mtcalarmd
install -m 755 -p -D %{_buildsubdir}/scripts/wipedisk %{buildroot}/%{local_bindir}/wipedisk
install -m 755 -p -D %{_buildsubdir}/fsync/fsync %{buildroot}/%{_sbindir}/fsync
install -m 700 -p -D %{_buildsubdir}/pmon/scripts/pmon-restart %{buildroot}/%{local_sbindir}/pmon-restart
install -m 700 -p -D %{_buildsubdir}/pmon/scripts/pmon-start %{buildroot}/%{local_sbindir}/pmon-start
install -m 700 -p -D %{_buildsubdir}/pmon/scripts/pmon-stop %{buildroot}/%{local_sbindir}/pmon-stop

# init script files
install -m 755 -p -D %{_buildsubdir}/scripts/mtcClient %{buildroot}%{_sysconfdir}/init.d/mtcClient
install -m 755 -p -D %{_buildsubdir}/scripts/hbsClient %{buildroot}%{_sysconfdir}/init.d/hbsClient
install -m 755 -p -D %{_buildsubdir}/hwmon/scripts/lsb/hwmon %{buildroot}%{_sysconfdir}/init.d/hwmon
install -m 755 -p -D %{_buildsubdir}/fsmon/scripts/fsmon %{buildroot}%{_sysconfdir}/init.d/fsmon
install -m 755 -p -D %{_buildsubdir}/scripts/mtclog %{buildroot}%{_sysconfdir}/init.d/mtclog
install -m 755 -p -D %{_buildsubdir}/pmon/scripts/pmon %{buildroot}%{_sysconfdir}/init.d/pmon
install -m 755 -p -D %{_buildsubdir}/lmon/scripts/lmon %{buildroot}%{_sysconfdir}/init.d/lmon
install -m 755 -p -D %{_buildsubdir}/hostw/scripts/hostw %{buildroot}%{_sysconfdir}/init.d/hostw
install -m 755 -p -D %{_buildsubdir}/alarm/scripts/mtcalarm.init %{buildroot}%{_sysconfdir}/init.d/mtcalarm


# TODO: Init hack. Should move to proper module
install -m 755 -p -D %{_buildsubdir}/scripts/hwclock.sh %{buildroot}%{_sysconfdir}/init.d/hwclock.sh
install -m 644 -p -D %{_buildsubdir}/scripts/hwclock.service %{buildroot}%{_unitdir}/hwclock.service

# systemd service files
install -m 644 -p -D %{_buildsubdir}/fsmon/scripts/fsmon.service %{buildroot}%{_unitdir}/fsmon.service
install -m 644 -p -D %{_buildsubdir}/hwmon/scripts/hwmon.service %{buildroot}%{_unitdir}/hwmon.service
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/pmon.service %{buildroot}%{_unitdir}/pmon.service
install -m 644 -p -D %{_buildsubdir}/hostw/scripts/hostw.service %{buildroot}%{_unitdir}/hostw.service
install -m 644 -p -D %{_buildsubdir}/scripts/mtcClient.service %{buildroot}%{_unitdir}/mtcClient.service
install -m 644 -p -D %{_buildsubdir}/scripts/hbsClient.service %{buildroot}%{_unitdir}/hbsClient.service
install -m 644 -p -D %{_buildsubdir}/scripts/mtclog.service %{buildroot}%{_unitdir}/mtclog.service
install -m 644 -p -D %{_buildsubdir}/scripts/goenabled.service %{buildroot}%{_unitdir}/goenabled.service
install -m 644 -p -D %{_buildsubdir}/scripts/runservices.service %{buildroot}%{_unitdir}/runservices.service
install -m 644 -p -D %{_buildsubdir}/alarm/scripts/mtcalarm.service %{buildroot}%{_unitdir}/mtcalarm.service
install -m 644 -p -D %{_buildsubdir}/lmon/scripts/lmon.service %{buildroot}%{_unitdir}/lmon.service

# go enabled stuff
install -m 755 -d %{buildroot}%{local_etc_goenabledd}
install -m 755 -p -D %{_buildsubdir}/scripts/goenabled %{buildroot}%{_sysconfdir}/init.d/goenabled

# start or stop services test script
install -m 755 -d %{buildroot}%{local_etc_servicesd}
install -m 755 -d %{buildroot}%{local_etc_servicesd}/controller
install -m 755 -d %{buildroot}%{local_etc_servicesd}/worker
install -m 755 -d %{buildroot}%{local_etc_servicesd}/storage
install -m 755 -p -D %{_buildsubdir}/scripts/mtcTest %{buildroot}/%{local_etc_servicesd}/worker
install -m 755 -p -D %{_buildsubdir}/scripts/mtcTest %{buildroot}/%{local_etc_servicesd}/controller
install -m 755 -p -D %{_buildsubdir}/scripts/mtcTest %{buildroot}/%{local_etc_servicesd}/storage
install -m 755 -p -D %{_buildsubdir}/scripts/runservices %{buildroot}%{_sysconfdir}/init.d/runservices

# test tools
install -m 755 -p -D %{_buildsubdir}/scripts/dmemchk.sh %{buildroot}%{local_sbindir}

# process monitor config files
install -m 755 -d %{buildroot}%{local_etc_pmond}
install -m 644 -p -D %{_buildsubdir}/scripts/mtcClient.conf %{buildroot}%{local_etc_pmond}/mtcClient.conf
install -m 644 -p -D %{_buildsubdir}/scripts/hbsClient.conf %{buildroot}%{local_etc_pmond}/hbsClient.conf
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/acpid.conf %{buildroot}%{local_etc_pmond}/acpid.conf
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/sshd.conf %{buildroot}%{local_etc_pmond}/sshd.conf
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/syslog-ng.conf %{buildroot}%{local_etc_pmond}/syslog-ng.conf
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/nslcd.conf %{buildroot}%{local_etc_pmond}/nslcd.conf
install -m 644 -p -D %{_buildsubdir}/fsmon/scripts/fsmon.conf %{buildroot}%{local_etc_pmond}/fsmon.conf
install -m 644 -p -D %{_buildsubdir}/scripts/mtclogd.conf %{buildroot}%{local_etc_pmond}/mtclogd.conf
install -m 644 -p -D %{_buildsubdir}/alarm/scripts/mtcalarm.pmon.conf %{buildroot}%{local_etc_pmond}/mtcalarm.conf
install -m 644 -p -D %{_buildsubdir}/lmon/scripts/lmon.pmon.conf %{buildroot}%{local_etc_pmond}/lmon.conf

# log rotation
install -m 755 -d %{buildroot}%{_sysconfdir}/logrotate.d
install -m 644 -p -D %{_buildsubdir}/scripts/mtce.logrotate %{buildroot}%{local_etc_logrotated}/mtce.logrotate
install -m 644 -p -D %{_buildsubdir}/hostw/scripts/hostw.logrotate %{buildroot}%{local_etc_logrotated}/hostw.logrotate
install -m 644 -p -D %{_buildsubdir}/pmon/scripts/pmon.logrotate %{buildroot}%{local_etc_logrotated}/pmon.logrotate
install -m 644 -p -D %{_buildsubdir}/lmon/scripts/lmon.logrotate %{buildroot}%{local_etc_logrotated}/lmon.logrotate
install -m 644 -p -D %{_buildsubdir}/fsmon/scripts/fsmon.logrotate %{buildroot}%{local_etc_logrotated}/fsmon.logrotate
install -m 644 -p -D %{_buildsubdir}/hwmon/scripts/hwmon.logrotate %{buildroot}%{local_etc_logrotated}/hwmon.logrotate
install -m 644 -p -D %{_buildsubdir}/alarm/scripts/mtcalarm.logrotate %{buildroot}%{local_etc_logrotated}/mtcalarm.logrotate

# software development files
install -m 644 -p -D %{_buildsubdir}/heartbeat/mtceHbsCluster.h %{buildroot}/%{_includedir}/mtceHbsCluster.h

install -m 755 -p -D %{_buildsubdir}/public/libamon.so.$MAJOR %{buildroot}%{_libdir}/libamon.so.$MAJOR
cd %{buildroot}%{_libdir} ; ln -s libamon.so.$MAJOR libamon.so.$MAJOR.$MINOR
cd %{buildroot}%{_libdir} ; ln -s libamon.so.$MAJOR libamon.so

# volatile directories
install -m 755 -d %{buildroot}/var
install -m 755 -d %{buildroot}/var/run

%pre
%service_add_pre fsmon.service fsmon.target
%service_add_pre mtcClient.service mtcClient.target
%service_add_pre hbsClient.service hbsClient.target
%service_add_pre mtclog.service mtclog.target
%service_add_pre goenabled.service goenabled.target
%service_add_pre mtcalarm.service mtcalarm.target
%service_add_pre runservices.service runservices.target
%service_add_pre hwclock.service hwclock.target

%preun
%service_del_preun fsmon.service fsmon.target
%service_del_preun mtcClient.service mtcClient.target
%service_del_preun hbsClient.service hbsClient.target
%service_del_preun mtclog.service mtclog.target
%service_del_preun goenabled.service goenabled.target
%service_del_preun mtcalarm.service mtcalarm.target
%service_del_preun runservices.service runservices.target
%service_del_preun hwclock.service hwclock.target

# Enable all services in systemd
%post
%service_add_post fsmon.service fsmon.target
%service_add_post mtcClient.service mtcClient.target
%service_add_post hbsClient.service hbsClient.target
%service_add_post mtclog.service mtclog.target
%service_add_post goenabled.service goenabled.target
%service_add_post mtcalarm.service mtcalarm.target
%service_add_post runservices.service runservices.target
%service_add_post hwclock.service hwclock.target
/bin/systemctl enable fsmon.service
/bin/systemctl enable mtcClient.service
/bin/systemctl enable hbsClient.service
/bin/systemctl enable mtclog.service
/bin/systemctl enable goenabled.service
/bin/systemctl enable mtcalarm.service

%postun
%service_del_postun fsmon.service fsmon.target
%service_del_postun mtcClient.service mtcClient.target
%service_del_postun hbsClient.service hbsClient.target
%service_del_postun mtclog.service mtclog.target
%service_del_postun goenabled.service goenabled.target
%service_del_postun mtcalarm.service mtcalarm.target
%service_del_postun runservices.service runservices.target
%service_del_postun hwclock.service hwclock.target

%pre -n mtce-hostw
%service_add_pre hostw.service hostw.target

%preun -n mtce-hostw
%service_del_preun hostw.service hostw.target

%post -n mtce-hostw
%service_add_post hostw.service hostw.target
/bin/systemctl enable hostw.service

%postun -n mtce-hostw
%service_del_postun hostw.service hostw.target


%pre -n libamon1
%service_add_pre pmon.service pmon.target

%preun -n libamon1
%service_del_preun pmon.service pmon.target

%post -n libamon1
%service_add_post pmon.service pmon.target
/sbin/ldconfig
/bin/systemctl enable pmon.service

%postun -n libamon1
%service_del_postun pmon.service pmon.target
/sbin/ldconfig

%pre -n mtce-lmon
%service_add_pre lmon.service lmon.target

%preun -n mtce-lmon
%service_del_preun lmon.service lmon.target

%post -n mtce-lmon
%service_add_post lmon.service lmon.target
/bin/systemctl enable lmon.service

%postun -n mtce-lmon
%service_del_postun lmon.service lmon.target

%pre -n mtce-hwmon
%service_add_pre hwmon.service hwmon.target

%preun -n mtce-hwmon
%service_del_preun hwmon.service hwmon.target

%post -n mtce-hwmon
%service_add_post hwmon.service hwmon.target
/bin/systemctl enable hwmon.service

%postun -n mtce-hwmon
%service_del_postun hwmon.service hwmon.target


###############################
# Maintenance RPM Files
###############################
%files
%license LICENSE
%defattr(-,root,root,-)

# create the mtc and its tmp dir
%dir %{_sysconfdir}/mtc
%dir %{_sysconfdir}/mtc/tmp
%dir %{local_etc_pmond}
%dir %{_sysconfdir}/bmc
%dir %{_sysconfdir}/bmc/server_profiles.d
%dir %{local_etc_servicesd}
%dir %{local_etc_servicesd}/controller
%dir %{local_etc_servicesd}/storage
%dir %{local_etc_servicesd}/worker
%dir %{_libdir}/ocf
%dir %{ocf_resourced}
%dir %{ocf_resourced}/platform


# SM OCF Start/Stop/Monitor Scripts
%{ocf_resourced}/platform/mtcAgent

# Config files
%config(noreplace)/etc/mtc.ini

# Config files - Non-Modifiable
%config %{_sysconfdir}/mtc.conf
%config %{_sysconfdir}/mtc/fsmond.conf

# Maintenance Process Monitor Config Files
%config %{local_etc_pmond}/sshd.conf
%config %{local_etc_pmond}/mtcClient.conf
%config %{local_etc_pmond}/acpid.conf
%config %{local_etc_pmond}/hbsClient.conf
%config %{local_etc_pmond}/syslog-ng.conf
%config %{local_etc_pmond}/fsmon.conf
%config %{local_etc_pmond}/mtclogd.conf
%config %{local_etc_pmond}/mtcalarm.conf
%config %{local_etc_pmond}/nslcd.conf

# Maintenance log rotation config files
%config %{local_etc_logrotated}/fsmon.logrotate
%config %{local_etc_logrotated}/mtce.logrotate
%config %{local_etc_logrotated}/mtcalarm.logrotate

# Maintenance start/stop services scripts
%config %{local_etc_servicesd}/controller/mtcTest
%config %{local_etc_servicesd}/storage/mtcTest
%config %{local_etc_servicesd}/worker/mtcTest

# BMC profile Files
%config %{bmc_profilesd}/sensor_hp360_v1_ilo_v4.profile
%config %{bmc_profilesd}/sensor_quanta_v1_ilo_v4.profile
%config %{bmc_profilesd}/sensor_hp380_v1_ilo_v4.profile

# Init scripts
%{_sysconfdir}/init.d/runservices
%{_sysconfdir}/init.d/goenabled
%{_sysconfdir}/init.d/fsmon
%{_sysconfdir}/init.d/mtclog
%{_sysconfdir}/init.d/hbsClient
%{_sysconfdir}/init.d/mtcClient
%{_sysconfdir}/init.d/mtcalarm
%{_sysconfdir}/init.d/hwclock.sh

%{_unitdir}/runservices.service
%{_unitdir}/goenabled.service
%{_unitdir}/mtclog.service
%{_unitdir}/mtcalarm.service
%{_unitdir}/fsmon.service
%{_unitdir}/mtcClient.service
%{_unitdir}/hbsClient.service
%{_unitdir}/hwclock.service

# Binaries
%{local_bindir}/mtcAgent
%{local_bindir}/fsmond
%{local_bindir}/hbsAgent
%{local_bindir}/mtclogd
%{local_bindir}/mtcalarmd
%{local_bindir}/hbsClient
%{local_bindir}/mtcClient
%{local_bindir}/wipedisk
%{local_sbindir}/dmemchk.sh
%{_sbindir}/fsync

###############################
# Process Monitor RPM Files
###############################
%files -n libamon1
%defattr(-,root,root,-)

# Config files - Non-Modifiable
%config %{_sysconfdir}/mtc/pmond.conf

%config %{local_etc_logrotated}/pmon.logrotate
%{_unitdir}/pmon.service
%{local_sbindir}/pmon-restart
%{local_sbindir}/pmon-start
%{local_sbindir}/pmon-stop

%{_libdir}/libamon.so.1.0
%{_libdir}/libamon.so.1

%{_sysconfdir}/init.d/pmon
%{local_bindir}/pmond

%files -n libamon1-devel
%defattr(-,root,root,-)
%{_libdir}/libamon.so

###############################
# Hardware Monitor RPM Files
###############################
%files -n mtce-hwmon
%defattr(-,root,root,-)

# Config files - Non-Modifiable
%config %{_sysconfdir}/mtc/hwmond.conf

%{_unitdir}/hwmon.service
%config %{local_etc_logrotated}/hwmon.logrotate
%{ocf_resourced}/platform/hwmon

%{_sysconfdir}/init.d/hwmon
%{local_bindir}/hwmond

###############################
# Host Watchdog RPM Files
###############################
%files -n mtce-hostw
%defattr(-,root,root,-)

# Config files - Non-Modifiable
%config %{_sysconfdir}/mtc/hostwd.conf

%config %{local_etc_logrotated}/hostw.logrotate
%{_unitdir}/hostw.service
%{_sysconfdir}/init.d/hostw
%{local_bindir}/hostwd

###############################
# Link Monitor RPM Files
###############################
%files -n mtce-lmon
%defattr(-,root,root,-)

# Config files - Non-Modifiable
%config %{_sysconfdir}/mtc/lmond.conf

%{_unitdir}/lmon.service
%config %{local_etc_logrotated}/lmon.logrotate
%config %{local_etc_pmond}/lmon.conf
%{local_bindir}/lmond
%{_sysconfdir}/init.d/lmon

###############################
# Maintenance Software Development RPM
###############################
%files -n mtce-devel
%defattr(-,root,root,-)

%{_includedir}/mtceHbsCluster.h

