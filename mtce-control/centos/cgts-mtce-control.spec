%define local_etc_pmond      /%{_sysconfdir}/pmond.d
%define local_etc_goenabledd /%{_sysconfdir}/goenabled.d

%define debug_package %{nil}

Name: cgts-mtce-control
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
Summary: Titanium Cloud Platform Controller Node Maintenance Package

Group: base
License: Apache-2.0
Packager: Wind River <info@windriver.com>
URL: unknown

Source0: %{name}-%{version}.tar.gz
Source1: goenabled
Source2: LICENSE

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

install -m 755 -d %{buildroot}%{local_etc}

# Controller-Only Process Monitor Config files
install -m 755 -d %{buildroot}%{local_etc_pmond}

# Controller-Only Go Enabled Test
install -m 755 -d                %{buildroot}%{local_etc_goenabledd}


%post
if [ $1 -eq 1 ] ; then
    /bin/systemctl enable lighttpd.service
    /bin/systemctl enable qemu_clean.service
fi
exit 0

%files
%license LICENSE
%defattr(-,root,root,-)

%clean
rm -rf $RPM_BUILD_ROOT
