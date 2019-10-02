Summary:        StarlingX Platform Storage Node Maintenance
Name:           mtce-storage
Version:        1.0.0
Release:        0
License:        Apache-2.0
Group:          Development/Tools/Other
URL:            https://opendev.org/starlingx/metal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  systemd
BuildRequires:  systemd-devel
BuildRequires:  insserv-compat
Requires:       /bin/systemctl
Requires:       bash

BuildArch: noarch

%description
Maintenance support files for storage-only node type.

%prep
%autosetup -n %{name}-%{version}/src

%build

%install
make install buildroot=%{buildroot} _sysconfdir=%{_sysconfdir} _unitdir=%{_unitdir} _datarootdir=%{_datarootdir}
install -dD -m 0755 %{buildroot}%{_sbindir}
ln -s /usr/sbin/service %{buildroot}%{_sbindir}/rcgoenabledStorage
ln -s /usr/sbin/service %{buildroot}%{_sbindir}/rcgoenabled-storage

%pre
%service_add_pre goenabled-storage.service

%post
%service_add_post goenabled-storage.service

%preun
%stop_on_removal
%service_del_preun goenabled-storage.service

%postun
%restart_on_update
%insserv_cleanup
%service_del_postun goenabled-storage.service

%files
%defattr(-,root,root,-)
%{_sysconfdir}/init.d/goenabledStorage
%{_sbindir}/rcgoenabledStorage
%{_sbindir}/rcgoenabled-storage
%{_unitdir}/goenabled-storage.service
%license %{_datarootdir}/licenses/mtce-storage-1.0/LICENSE
%{_datadir}/licenses/mtce-storage-1.0

%changelog
