%define local_etc_pmond      /%{_sysconfdir}/pmond.d
%define local_etc_goenabledd /%{_sysconfdir}/goenabled.d
%define local_etc_servicesd  /%{_sysconfdir}/services.d

Name: cgts-mtce-storage
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
Summary: Titanium Cloud Platform Storage Node Maintenance Package

Group: base
License: Apache-2.0
Packager: Wind River <info@windriver.com>
URL: unknown

Source0: %{name}-%{version}.tar.gz
Source1: goenabled
Source2: goenabled-storage.service
Source3: LICENSE

BuildRequires: systemd
BuildRequires: systemd-devel
Requires: bash
Requires: /bin/systemctl

%description
Maintenance support files for storage-only node type

%prep

%build

%install


# Storage-Only Init Scripts
install -m 755 -p -D %{SOURCE1} %{buildroot}/etc/init.d/goenabledStorage

# Storage-Only Process Monitor Config files
install -m 755 -d %{buildroot}%{local_etc_pmond}

# Storage-Only Go Enabled Tests
install -m 755 -d %{buildroot}%{local_etc_goenabledd}

# Storage-Only Services
install -m 755 -d %{buildroot}%{local_etc_servicesd}/storage

# Install systemd dir
install -m 644 -p -D %{SOURCE2} %{buildroot}%{_unitdir}/goenabled-storage.service

%post
/bin/systemctl enable goenabled-storage.service

%files

%defattr(-,root,root,-)

/etc/init.d/goenabledStorage
%{_unitdir}/goenabled-storage.service

%clean
rm -rf $RPM_BUILD_ROOT
