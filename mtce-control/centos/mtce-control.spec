%define debug_package %{nil}

Name: mtce-control
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
Summary: Titanium Cloud Platform Controller Node Maintenance Package

Group: base
License: Apache-2.0
Packager: Wind River <info@windriver.com>
URL: unknown

Source0: %{name}-%{version}.tar.gz

BuildRequires: systemd
BuildRequires: systemd-devel
Requires: bash
Requires: /bin/systemctl
Requires: lighttpd
Requires: qemu-kvm-ev

%description
Maintenance support files for controller-only node type

%prep
%setup

%build

%install
make install buildroot=%{buildroot} _sysconfdir=%{_sysconfdir} _unitdir=%{_unitdir} _datarootdir=%{_datarootdir}

%post
if [ $1 -eq 1 ] ; then
    /bin/systemctl enable lighttpd.service
    /bin/systemctl enable qemu_clean.service
fi
exit 0

%files
%license LICENSE
%defattr(-,root,root,-)
%{_sysconfdir}/init.d/goenabledControl
%{_datarootdir}/licenses/mtce-control-1.0/LICENSE

%clean
rm -rf $RPM_BUILD_ROOT
