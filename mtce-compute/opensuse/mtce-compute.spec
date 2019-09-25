%define local_etc_goenabledd %{_sysconfdir}/goenabled.d
Summary: Compute Node Maintenance Package
Name: mtce-compute
Version: 1.0.0
Release: 1
License: Apache-2.0
Group: Development/Tools/Other
URL: https://opendev.org/starlingx/metal
Source0: %{name}-%{version}.tar.gz

BuildArch: noarch
BuildRequires: systemd
BuildRequires: systemd-devel
Requires: bash
Requires: systemd
Requires: qemu-kvm

%description
Maintenance support files for compute-only node type

%prep
%autosetup -n %{name}-%{version}/src

%build

%install
make install buildroot=%{buildroot} _sysconfdir=%{_sysconfdir} _unitdir=%{_unitdir} _datarootdir=%{_datarootdir}

%pre
%service_add_pre goenabled-worker.service goenabled-worker.target

%post
%service_add_post goenabled-worker.service goenabled-worker.target
/bin/systemctl enable goenabled-worker.service

%preun
%service_del_preun goenabled-worker.service goenabled-worker.target

%postun
%service_del_postun goenabled-worker.service goenabled-worker.target

%files
%defattr(-,root,root,-)
%dir %{_sysconfdir}/goenabled.d
%dir %{_datarootdir}/licenses/mtce-compute-1.0
%{_sysconfdir}/init.d/goenabledWorker
%{local_etc_goenabledd}/virt-support-goenabled.sh
%{_unitdir}/goenabled-worker.service
%license %{_datarootdir}/licenses/mtce-compute-1.0/LICENSE

%changelog
