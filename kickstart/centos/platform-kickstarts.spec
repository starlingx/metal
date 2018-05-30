Name:           platform-kickstarts
Version:        1.0.0
Release:        %{tis_patch_ver}%{?_tis_dist}
Summary:        Platform Kickstarts
License:        Apache-2.0
Packager:       Wind River <info@windriver.com>
URL:            unknown

Source0:        %{name}-%{version}.tar.gz
Source1:        LICENSE

BuildArch:      noarch

%description
Platform kickstart files

BuildRequires:    perl
BuildRequires:    perl(Getopt::Long)
BuildRequires:    perl(POSIX)

%define feed_dir /www/pages/feed/rel-%{platform_release}

%prep
%setup

%build
./centos-ks-gen.pl --release %{platform_release}
cp %{SOURCE1} .

%install

install -d -m 0755 %{buildroot}%{feed_dir}
install -m 0444 generated/* %{buildroot}%{feed_dir}/

install -d -m 0755 %{buildroot}/pxeboot
install -D -m 0444 pxeboot/* %{buildroot}/pxeboot

install -d -m 0755 %{buildroot}/extra_cfgs
install -D -m 0444 extra_cfgs/* %{buildroot}/extra_cfgs

%files
%defattr(-,root,root,-)
%license LICENSE
%{feed_dir}

%package pxeboot
Summary: Kickstarts for pxeboot server

%description pxeboot
Kickstarts for pxeboot server

%files pxeboot
%defattr(-,root,root,-)
/pxeboot/

%package extracfgs
Summary: Extra lab-usage kickstarts

%description extracfgs
Extra lab-usage kickstarts

%files extracfgs
%defattr(-,root,root,-)
/extra_cfgs/
