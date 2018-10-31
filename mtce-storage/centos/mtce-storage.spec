%define debug_package %{nil}

Name: mtce-storage
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
Summary: Titanium Cloud Platform Storage Node Maintenance Package

Group: base
License: Apache-2.0
Packager: Wind River <info@windriver.com>
URL: unknown

Source0: %{name}-%{version}.tar.gz

BuildRequires: systemd
BuildRequires: systemd-devel
Requires: bash
Requires: /bin/systemctl

%description
Maintenance support files for storage-only node type

%prep
%setup

%build

%install
make install buildroot=%{buildroot} _sysconfdir=%{_sysconfdir} _unitdir=%{_unitdir} _datarootdir=%{_datarootdir}

%post
/bin/systemctl enable goenabled-storage.service

%files
%defattr(-,root,root,-)
%{_sysconfdir}/init.d/goenabledStorage
%{_unitdir}/goenabled-storage.service
%license %{_datarootdir}/licenses/mtce-storage-1.0/LICENSE

%clean
rm -rf $RPM_BUILD_ROOT
