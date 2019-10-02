%global pypi_name inventoryclient
Name:           python-inventoryclient
Version:        1.0.0
Release:        1
Summary:        A python client library for Inventory
License:        Apache-2.0
Group:          base
URL:            https://opendev.org/starlingx/metal
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  git
BuildRequires:  python-pbr >= 2.0.0
BuildRequires:  python-setuptools
Requires:       bash-completion
Requires:       python-keystoneauth1 >= 3.1.0
Requires:       python2-oslo.i18n >= 2.1.0
Requires:       python2-oslo.utils >= 3.20.0
Requires:       python2-pbr >= 2.0.0
Requires:       python2-requests
Requires:       python2-six >= 1.9.0
%if 0%{?suse_version}
BuildRequires:  python-pip
%else
BuildRequires:  python2-pip
%endif

%description
This package is a python client library for Inventory

%define local_bindir %{_bindir}/
%define local_etc_bash_completiond %{_sysconfdir}/bash_completion.d/
%define pythonroot %{_libdir}/python2.7/site-packages
%define debug_package %{nil}

%package          sdk
Summary:        SDK files for %{name}

%description      sdk
This package contains SDK files for %{name} package.

%prep
%autosetup -n %{name}-%{version}/inventoryclient

# Remove bundled egg-info
rm -rf *.egg-info

%build
echo "Start build"
export PBR_VERSION=%{version}
python setup.py build

%install
echo "Start install"
export PBR_VERSION=%{version}
python setup.py install --root=%{buildroot} \
                             --install-lib=%{pythonroot} \
                             --prefix=%{_prefix} \
                             --install-data=%{_datadir} \
                             --single-version-externally-managed

install -d -m 755 %{buildroot}%{local_etc_bash_completiond}
install -p -D -m 664 tools/inventory.bash_completion %{buildroot}%{local_etc_bash_completiond}/inventory.bash_completion

chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/pci_device_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/ethernetport_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/node_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/lldp_agent_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/common/options.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/cpu_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/memory_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/lldp_neighbour_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/port_shell.py
chmod a+x %{buildroot}/%{pythonroot}/inventoryclient/v1/host_shell.py

%files
%defattr(-,root,root,-)
%license LICENSE
%{local_bindir}/*
%config %{local_etc_bash_completiond}/*
%{pythonroot}/%{pypi_name}/*
%{pythonroot}/%{pypi_name}-%{version}*.egg-info
%dir %{pythonroot}/inventoryclient

%changelog
