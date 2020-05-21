Summary: StarlingX Network Installation
Name: pxe-network-installer
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
License: Apache-2.0
Group: base
Packager: Wind River <info@windriver.com>
URL: unknown

Source0: LICENSE

Source001: vmlinuz
Source002: initrd.img
Source003: squashfs.img

Source010: pxeboot-update.sh
Source011: grub.cfg
Source012: efiboot.img

Source030: default
Source031: default.static
Source032: centos-pxe-controller-install
Source033: centos-pxe-worker-install
Source034: centos-pxe-smallsystem-install
Source035: centos-pxe-storage-install
Source036: centos-pxe-worker_lowlatency-install
Source037: centos-pxe-smallsystem_lowlatency-install

Source050: pxe-grub.cfg
Source051: pxe-grub.cfg.static
Source052: efi-centos-pxe-controller-install
Source053: efi-centos-pxe-worker-install
Source054: efi-centos-pxe-smallsystem-install
Source055: efi-centos-pxe-storage-install
Source056: efi-centos-pxe-worker_lowlatency-install
Source057: efi-centos-pxe-smallsystem_lowlatency-install


BuildRequires: syslinux
BuildRequires: grub2
BuildRequires: grub2-efi-x64-pxeboot

Requires: grub2-efi-x64-pxeboot

%description
StarlingX Network Installation

%files
%defattr(-,root,root,-)

%install
install -v -d -m 755 %{buildroot}/pxeboot
install -v -d -m 755 %{buildroot}/pxeboot/pxelinux.cfg.files
install -v -d -m 755 %{buildroot}/pxeboot/rel-%{platform_release}
install -v -d -m 755 %{buildroot}/pxeboot/EFI
install -v -d -m 755 %{buildroot}/pxeboot/EFI/centos
ln -s %{_prefix}/lib/grub/x86_64-efi %{buildroot}/pxeboot/EFI/centos/x86_64-efi

install -v -m 644 %{_sourcedir}/vmlinuz \
    %{buildroot}/pxeboot/rel-%{platform_release}/installer-bzImage_1.0
install -v -m 644 %{_sourcedir}/initrd.img \
    %{buildroot}/pxeboot/rel-%{platform_release}/installer-intel-x86-64-initrd_1.0
ln -s installer-bzImage_1.0 %{buildroot}/pxeboot/rel-%{platform_release}/installer-bzImage
ln -s installer-intel-x86-64-initrd_1.0 %{buildroot}/pxeboot/rel-%{platform_release}/installer-initrd

install -v -D -m 644 %{_sourcedir}/squashfs.img \
    %{buildroot}/www/pages/feed/rel-%{platform_release}/LiveOS/squashfs.img

install -v -d -m 755 %{buildroot}%{_sbindir}

install -v -m 755 %{_sourcedir}/pxeboot-update.sh %{buildroot}%{_sbindir}/pxeboot-update-%{platform_release}.sh

install -v -m 644 %{_sourcedir}/default \
    %{buildroot}/pxeboot/pxelinux.cfg.files/default
install -v -m 644 %{_sourcedir}/default.static \
    %{buildroot}/pxeboot/pxelinux.cfg.files/default.static
install -v -m 644 %{_sourcedir}/centos-pxe-controller-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-controller-install-%{platform_release}
install -v -m 644 %{_sourcedir}/centos-pxe-worker-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-worker-install-%{platform_release}
install -v -m 644 %{_sourcedir}/centos-pxe-smallsystem-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-smallsystem-install-%{platform_release}
install -v -m 644 %{_sourcedir}/centos-pxe-storage-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-storage-install-%{platform_release}
install -v -m 644 %{_sourcedir}/centos-pxe-worker_lowlatency-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-worker_lowlatency-install-%{platform_release}
install -v -m 644 %{_sourcedir}/centos-pxe-smallsystem_lowlatency-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-smallsystem_lowlatency-install-%{platform_release}


# UEFI support
install -v -m 644 %{_sourcedir}/pxe-grub.cfg \
    %{buildroot}/pxeboot/pxelinux.cfg.files/grub.cfg
install -v -m 644 %{_sourcedir}/pxe-grub.cfg.static \
    %{buildroot}/pxeboot/pxelinux.cfg.files/grub.cfg.static
# Copy EFI boot image. It will be used to create ISO on the Controller.
install -v -m 644 %{_sourcedir}/efiboot.img \
    %{buildroot}/pxeboot/rel-%{platform_release}/
install -v -m 644 %{_sourcedir}/efi-centos-pxe-controller-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-controller-install-%{platform_release}
install -v -m 644 %{_sourcedir}/efi-centos-pxe-worker-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-worker-install-%{platform_release}
install -v -m 644 %{_sourcedir}/efi-centos-pxe-smallsystem-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-smallsystem-install-%{platform_release}
install -v -m 644 %{_sourcedir}/efi-centos-pxe-storage-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-storage-install-%{platform_release}
install -v -m 644 %{_sourcedir}/efi-centos-pxe-worker_lowlatency-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-worker_lowlatency-install-%{platform_release}
install -v -m 644 %{_sourcedir}/efi-centos-pxe-smallsystem_lowlatency-install \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-smallsystem_lowlatency-install-%{platform_release}


sed -i "s/xxxSW_VERSIONxxx/%{platform_release}/g" \
    %{buildroot}/pxeboot/pxelinux.cfg.files/pxe-* \
    %{buildroot}/pxeboot/pxelinux.cfg.files/efi-pxe-*

# Copy files from the syslinux pkg
install -v -m 0644 \
    %{_datadir}/syslinux/menu.c32 \
    %{_datadir}/syslinux/vesamenu.c32 \
    %{_datadir}/syslinux/chain.c32 \
    %{_datadir}/syslinux/linux.c32 \
    %{_datadir}/syslinux/reboot.c32 \
    %{_datadir}/syslinux/pxechain.com \
    %{_datadir}/syslinux/pxelinux.0 \
    %{_datadir}/syslinux/gpxelinux.0 \
    %{buildroot}/pxeboot

# Copy StarlingX grub.cfg. It will be used to create ISO on the Controller.
install -v -m 0644 %{_sourcedir}/grub.cfg \
    %{buildroot}/pxeboot/EFI/

# UEFI bootloader expect the grub.cfg file to be in /pxeboot/ so create a symlink for it
ln -s pxelinux.cfg/grub.cfg %{buildroot}/pxeboot/grub.cfg

%files
%license ../SOURCES/LICENSE
%defattr(-,root,root,-)
%dir /pxeboot
/pxeboot/*
%{_sbindir}/pxeboot-update-%{platform_release}.sh
/www/pages/feed/rel-%{platform_release}/LiveOS/squashfs.img

