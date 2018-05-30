%define local_etc_pmond      %{_sysconfdir}/pmon.d
%define local_etc_goenabledd %{_sysconfdir}/goenabled.d
%define local_etc_nova       %{_sysconfdir}/nova

%define debug_package %{nil}

Name: cgts-mtce-compute
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
Summary: Compute Node Maintenance Package

Group: base
License: Apache-2.0
Packager: Wind River <info@windriver.com>
URL: unknown

Source0: %{name}-%{version}.tar.gz
Source1: goenabled
Source2: e_nova-init
Source3: nova-cleanup
Source4: nova-startup
Source11: nova-cleanup.conf
Source12: nova-compute.conf
Source14: libvirtd.conf
Source21: nova-goenabled.sh
Source22: virt-support-goenabled.sh
Source31: goenabled-compute.service
Source32: e_nova-init.service

BuildRequires: systemd
BuildRequires: systemd-devel
Requires: bash
Requires: /bin/systemctl
Requires: qemu-kvm-ev

%description
Maintenance support files for compute-only node type

%prep
%setup

%build

%install

# Compute-Only Init Scripts (source group x)
install -m 755 -p -D %{SOURCE1} %{buildroot}/etc/init.d/goenabledCompute
install -m 755 -p -D %{SOURCE2} %{buildroot}/etc/init.d/e_nova-init
install -m 755 -p -D %{SOURCE3} %{buildroot}/etc/init.d/nova-cleanup
install -m 755 -p -D %{SOURCE4} %{buildroot}/etc/init.d/nova-startup

# Compute-Only Process Monitor Config files (source group 1x)
install -m 755 -d                %{buildroot}%{local_etc_pmond}
install -m 755 -d                %{buildroot}%{local_etc_nova}
install -m 644 -p -D %{SOURCE11} %{buildroot}%{local_etc_nova}/nova-cleanup.conf
install -m 644 -p -D %{SOURCE12} %{buildroot}%{local_etc_nova}/nova-compute.conf
install -m 644 -p -D %{SOURCE14} %{buildroot}%{local_etc_pmond}/libvirtd.conf

# Compute-Only Go Enabled Test (source group 2x)
install -m 755 -d                %{buildroot}%{local_etc_goenabledd}
install -m 755 -p -D %{SOURCE21} %{buildroot}%{local_etc_goenabledd}/nova-goenabled.sh
install -m 755 -p -D %{SOURCE22} %{buildroot}%{local_etc_goenabledd}/virt-support-goenabled.sh

# Install to systemd (source group 3x)
install -m 644 -p -D %{SOURCE31} %{buildroot}%{_unitdir}/goenabled-compute.service
install -m 644 -p -D %{SOURCE32} %{buildroot}%{_unitdir}/e_nova-init.service

%post
/bin/systemctl enable goenabled-compute.service
/bin/systemctl enable e_nova-init.service
/bin/systemctl enable qemu_clean.service

%files
%license LICENSE

%defattr(-,root,root,-)

/etc/init.d/goenabledCompute
/etc/init.d/e_nova-init
/etc/init.d/nova-cleanup
/etc/init.d/nova-startup

%{local_etc_nova}/nova-cleanup.conf
%{local_etc_nova}/nova-compute.conf
%{local_etc_pmond}/libvirtd.conf

%{local_etc_goenabledd}/nova-goenabled.sh
%{local_etc_goenabledd}/virt-support-goenabled.sh

%{_unitdir}/goenabled-compute.service
%{_unitdir}/e_nova-init.service

%clean
rm -rf $RPM_BUILD_ROOT
