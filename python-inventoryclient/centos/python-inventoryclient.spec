%global pypi_name inventoryclient

Summary: A python client library for Inventory
Name: python-inventoryclient
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
License: Apache-2.0
Group: base
Packager: Wind River <info@windriver.com>
URL: unknown
Source0: %{name}-%{version}.tar.gz

BuildRequires:  git
BuildRequires:  python-pbr >= 2.0.0
BuildRequires:  python-setuptools
BuildRequires:  python2-pip

Requires:       python-keystoneauth1 >= 3.1.0
Requires:       python-pbr >= 2.0.0
Requires:       python-six >= 1.9.0
Requires:       python-oslo-i18n >= 2.1.0
Requires:       python-oslo-utils >= 3.20.0
Requires:       python-requests
Requires:       bash-completion

%description
A python client library for Inventory

%define local_bindir /usr/bin/
%define local_etc_bash_completiond /etc/bash_completion.d/
%define pythonroot /usr/lib64/python2.7/site-packages

%define debug_package %{nil}

%prep
%autosetup -n %{name}-%{version} -S git

# Remove bundled egg-info
rm -rf *.egg-info

%build
echo "Start build"

export PBR_VERSION=%{version}
%{__python} setup.py build

%install
echo "Start install"
export PBR_VERSION=%{version}
%{__python} setup.py install --root=%{buildroot} \
                             --install-lib=%{pythonroot} \
                             --prefix=/usr \
                             --install-data=/usr/share \
                             --single-version-externally-managed

install -d -m 755 %{buildroot}%{local_etc_bash_completiond}
install -p -D -m 664 tools/inventory.bash_completion %{buildroot}%{local_etc_bash_completiond}/inventory.bash_completion


%clean
echo "CLEAN CALLED"
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%doc LICENSE
%{local_bindir}/*
%{local_etc_bash_completiond}/*
%{pythonroot}/%{pypi_name}/*
%{pythonroot}/%{pypi_name}-%{version}*.egg-info
