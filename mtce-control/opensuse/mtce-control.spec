Summary: Controller Node Maintenance Package
Name: mtce-control
Version: 1.0.0
Release: 1
License: Apache-2.0
Group: Development/Tools/Other
URL: https://opendev.org/starlingx/metal
Source0: %{name}-%{version}.tar.gz

BuildArch: noarch
BuildRequires: systemd
BuildRequires: systemd-devel
BuildRequires: systemd-sysvinit
Requires: bash
Requires: systemd
Requires: lighttpd
Requires: qemu-kvm

%description
Maintenance support files for controller-only node type

%prep
%autosetup -n %{name}-%{version}/src

%build

%install
make install buildroot=%{buildroot} _sysconfdir=%{_sysconfdir} _unitdir=%{_unitdir} _datarootdir=%{_datarootdir}

%pre
%service_add_pre hbsAgent.service hbsAgent.target

%post
%service_add_post hbsAgent.service hbsAgent.target
if [ $1 -eq 1 ] ; then
    /bin/systemctl enable lighttpd.service
    /bin/systemctl enable qemu_clean.service
    /bin/systemctl enable hbsAgent.service
fi
exit 0

%preun
%service_del_preun hbsAgent.service hbsAgent.target

%postun
%service_del_postun hbsAgent.service hbsAgent.target

%files
%dir %{_sysconfdir}/pmon.d
%dir %{_datadir}/licenses/mtce-control-1.0
%defattr(-,root,root,-)
%{_sysconfdir}/init.d/goenabledControl
%license %{_datarootdir}/licenses/mtce-control-1.0/LICENSE
%config %{_sysconfdir}/pmon.d/hbsAgent.conf
%{_sysconfdir}/init.d/hbsAgent
%{_unitdir}/hbsAgent.service

%changelog
