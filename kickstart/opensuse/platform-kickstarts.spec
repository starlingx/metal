%define platform_release 1
%define feed_dir /var/www/pages/feed/rel-%{platform_release}

Summary:        Platform Kickstarts
Name:           platform-kickstarts
Version:        1.0
Release:        0
License:        Apache-2.0
Group:          Development/Tools/Other
URL:            https://opendev.org/starlingx/metal
Source0:        bsp-files-%{version}.tar.gz
Source1:        LICENSE

BuildRequires:  perl
BuildRequires:  perl(Getopt::Long)
BuildRequires:  perl(POSIX)

BuildArch:      noarch

%description
Platform kickstart files

%prep
%autosetup -n bsp-files-%{version}

%build
./centos-ks-gen.pl --release %{platform_release}
cp %{SOURCE1} .

%install
install -d -m 0755 %{buildroot}%{feed_dir}
install -m 0444 generated/* %{buildroot}%{feed_dir}

install -d -m 0755 %{buildroot}/pxeboot
install -D -m 0444 pxeboot/* %{buildroot}/pxeboot

install -d -m 0755 %{buildroot}/extra_cfgs
install -D -m 0444 extra_cfgs/* %{buildroot}/extra_cfgs

%files
%defattr(-,root,root,-)
%license LICENSE
%{feed_dir}

%files
%defattr(-,root,root,-)
/var/www
/var/www/pages
/var/www/pages/feed

%package pxeboot
Summary:        Kickstarts Pxeboot Server

%description pxeboot
Kickstarts for Pxeboot server

%files pxeboot
%defattr(-,root,root,-)
/pxeboot/

%package extracfgs
Summary:        Extra Lab-usage Kickstarts

%description extracfgs
Extra lab-usage kickstarts configuration

%files extracfgs
%defattr(-,root,root,-)
/extra_cfgs/

%changelog
