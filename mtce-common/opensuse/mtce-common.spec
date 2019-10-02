Name:           mtce-common
Version:        1.0.0
Release:        1
Summary:        Maintenance Common Base Package
License:        Apache-2.0
Group:          Development/Tools/Other
URL:            https://opendev.org/starlingx/metal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  expect
BuildRequires:  fm-common
BuildRequires:  fm-common-dev
BuildRequires:  fm-mgr
BuildRequires:  libevent
BuildRequires:  libevent-devel
BuildRequires:  libuuid-devel
BuildRequires:  openssl
BuildRequires:  openssl-devel
BuildRequires:  postgresql
BuildRequires:  systemd-devel
BuildRequires:  cppcheck
Requires:       %{_bindir}/expect
Requires:       /bin/bash
Requires:       /bin/sh
Requires:       /bin/systemctl
Requires:       dpkg
Requires:       expect
Requires:       fm-common >= 1.0
Requires:       libc.so.6()(64bit)
Requires:       libc.so.6(GLIBC_2.14)(64bit)
Requires:       libc.so.6(GLIBC_2.2.5)(64bit)
Requires:       libc.so.6(GLIBC_2.3)(64bit)
Requires:       libc.so.6(GLIBC_2.4)(64bit)
Requires:       libevent >= 2.0.21
Requires:       libevent-2.0.so.5()(64bit)
Requires:       libfmcommon.so.1()(64bit)
Requires:       libgcc_s.so.1()(64bit)
Requires:       libgcc_s.so.1(GCC_3.0)(64bit)
Requires:       libjson-c.so.2()(64bit)
Requires:       libm.so.6()(64bit)
Requires:       libpthread.so.0()(64bit)
Requires:       libpthread.so.0(GLIBC_2.2.5)(64bit)
Requires:       librt.so.1()(64bit)
Requires:       librt.so.1(GLIBC_2.2.5)(64bit)
Requires:       librt.so.1(GLIBC_2.3.3)(64bit)
Requires:       libssh2.so.1()(64bit)
Requires:       libstdc++.so.6()(64bit)
Requires:       libstdc++.so.6(CXXABI_1.3)(64bit)
Requires:       libstdc++.so.6(GLIBCXX_3.4)(64bit)
Requires:       libstdc++.so.6(GLIBCXX_3.4.11)(64bit)
Requires:       libstdc++.so.6(GLIBCXX_3.4.14)(64bit)
Requires:       libstdc++.so.6(GLIBCXX_3.4.15)(64bit)
Requires:       libstdc++.so.6(GLIBCXX_3.4.9)(64bit)
Requires:       libuuid.so.1()(64bit)
Requires:       python-rtslib
Requires:       rtld(GNU_HASH)
Requires:       time
Requires:       util-linux
%if 0%{?suse_version}
BuildRequires:  gcc-c++
BuildRequires:  libjansson4
BuildRequires:  libjson-c-devel
BuildRequires:  libssh2-1
BuildRequires:  libssh2-devel
BuildRequires:  smartmontools
%else
BuildRequires:  cppcheck
BuildRequires:  json-c
BuildRequires:  json-c-devel
BuildRequires:  libssh2
BuildRequires:  libssh2-devel
%endif

%description
Maintenance Common Base Package - development files

%package -n mtce-common-devel
Summary:        Maintenance Common Base - development files
Group:          Development/Tools/Other
Provides:       mtce-common-devel = %{version}-%{release}

%description -n mtce-common-devel
Maintenance Common Base. This package contains header files,
and related items necessary for software development.

%define debug_package %{nil}

%prep
%autosetup -n %{name}-%{version}/src

%build
VER=%{version}
MAJOR=$(echo $VER | awk -F . '{print $1}')
MINOR=$(echo $VER | awk -F . '{print $2}')
make MAJOR=$MAJOR MINOR=$MINOR %{?_smp_mflags} build

%global _buildsubdir %{_builddir}/%{name}-%{version}/src

%install
VER=%{version}
MAJOR=$(echo $VER | awk -F . '{print $1}')
MINOR=$(echo $VER | awk -F . '{print $2}')

install -m 755 -d %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/daemon/libdaemon.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libcommon.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libthreadUtil.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libbmcUtils.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libpingUtil.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libnodeBase.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libregexUtil.a %{buildroot}%{_libdir}
install -m 644 -p -D %{_buildsubdir}/common/libhostUtil.a %{buildroot}%{_libdir}

# mtce-common headers required to bring in nodeBase.h
install -m 755 -d %{buildroot}%{_includedir}
install -m 755 -d %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/fitCodes.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/logMacros.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/returnCodes.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/nodeTimers.h %{buildroot}%{_includedir}/mtce-common

# mtce-common headers required to build mtce-guest
install -m 644 -p -D %{_buildsubdir}/common/hostClass.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/httpUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/jsonUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/msgClass.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/nodeBase.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/nodeEvent.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/nodeMacro.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/nodeUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/timeUtil.h %{buildroot}%{_includedir}/mtce-common

# mtce-daemon headers required to build mtce-guest
install -m 755 -d %{buildroot}%{_includedir}/mtce-daemon
install -m 644 -p -D %{_buildsubdir}/daemon/daemon_ini.h %{buildroot}%{_includedir}/mtce-daemon
install -m 644 -p -D %{_buildsubdir}/daemon/daemon_common.h %{buildroot}%{_includedir}/mtce-daemon
install -m 644 -p -D %{_buildsubdir}/daemon/daemon_option.h %{buildroot}%{_includedir}/mtce-daemon

# remaining mtce-common headers required to build mtce
install -m 644 -p -D %{_buildsubdir}/common/alarmUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/hostUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/redfishUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/bmcUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/ipmiUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/nlEvent.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/pingUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/regexUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/threadUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/tokenUtil.h %{buildroot}%{_includedir}/mtce-common
install -m 644 -p -D %{_buildsubdir}/common/secretUtil.h %{buildroot}%{_includedir}/mtce-common

%post

%files -n mtce-common-devel
%defattr(-,root,root,-)
%{_includedir}/mtce-common/*.h
%{_includedir}/mtce-daemon/*.h
%{_libdir}/*.a
%dir %{_includedir}/mtce-common
%dir %{_includedir}/mtce-daemon

%changelog
