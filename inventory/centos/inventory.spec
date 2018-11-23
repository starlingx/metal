Summary: Inventory
Name: inventory
Version: 1.0
Release: %{tis_patch_ver}%{?_tis_dist}
License: Apache-2.0
Group: base
Packager: Wind River <info@windriver.com>
URL: unknown
Source0: %{name}-%{version}.tar.gz

BuildRequires:  cgts-client
BuildRequires:  python-setuptools
BuildRequires:  python-jsonpatch
BuildRequires:  python-keystoneauth1
BuildRequires:  python-keystonemiddleware
BuildRequires:  python-mock
BuildRequires:  python-neutronclient
BuildRequires:  python-oslo-concurrency
BuildRequires:  python-oslo-config
BuildRequires:  python-oslo-context
BuildRequires:  python-oslo-db
BuildRequires:  python-oslo-db-tests
BuildRequires:  python-oslo-i18n
BuildRequires:  python-oslo-log
BuildRequires:  python-oslo-messaging
BuildRequires:  python-oslo-middleware
BuildRequires:  python-oslo-policy
BuildRequires:  python-oslo-rootwrap
BuildRequires:  python-oslo-serialization
BuildRequires:  python-oslo-service
BuildRequires:  python-oslo-utils
BuildRequires:  python-oslo-versionedobjects
BuildRequires:  python-oslotest
BuildRequires:  python-osprofiler
BuildRequires:  python-os-testr
BuildRequires:  python-pbr
BuildRequires:  python-pecan
BuildRequires:  python-psutil
BuildRequires:  python-requests
BuildRequires:  python-retrying
BuildRequires:  python-six
BuildRequires:  python-sqlalchemy
BuildRequires:  python-stevedore
BuildRequires:  python-webob
BuildRequires:  python-wsme
BuildRequires:  systemd
BuildRequires:  systemd-devel


Requires:   python-pyudev
Requires:   pyparted
Requires:   python-ipaddr
Requires:   python-paste
Requires:   python-eventlet
Requires:   python-futurist >= 0.11.0
Requires:   python-jsonpatch
Requires:   python-keystoneauth1 >= 3.1.0
Requires:   python-keystonemiddleware >= 4.12.0
Requires:   python-neutronclient >= 6.3.0
Requires:   python-oslo-concurrency >= 3.8.0
Requires:   python-oslo-config >= 2:4.0.0
Requires:   python-oslo-context >= 2.14.0
Requires:   python-oslo-db >= 4.24.0
Requires:   python-oslo-i18n >= 2.1.0
Requires:   python-oslo-log >= 3.22.0
Requires:   python-oslo-messaging >= 5.24.2
Requires:   python-oslo-middleware >= 3.27.0
Requires:   python-oslo-policy >= 1.23.0
Requires:   python-oslo-rootwrap >= 5.0.0
Requires:   python-oslo-serialization >= 1.10.0
Requires:   python-oslo-service >= 1.10.0
Requires:   python-oslo-utils >= 3.20.0
Requires:   python-oslo-versionedobjects >= 1.17.0
Requires:   python-osprofiler >= 1.4.0
Requires:   python-pbr
Requires:   python-pecan
Requires:   python-psutil
Requires:   python-requests
Requires:   python-retrying
Requires:   python-six
Requires:   python-sqlalchemy
Requires:   python-stevedore >= 1.20.0
Requires:   python-webob >= 1.7.1
Requires:   python-wsme

%description
Inventory Service

%define local_bindir         /usr/bin/
%define local_etc_goenabledd /etc/goenabled.d/
%define local_etc_inventory  /etc/inventory/
%define local_etc_motdd      /etc/motd.d/
%define pythonroot           /usr/lib64/python2.7/site-packages
%define ocf_resourced        /usr/lib/ocf/resource.d

%define local_etc_initd /etc/init.d/
%define local_etc_pmond /etc/pmon.d/

%define debug_package %{nil}

%prep
%setup

# Remove bundled egg-info
rm -rf *.egg-info

%build
echo "Start inventory build"
export PBR_VERSION=%{version}
%{__python} setup.py build
PYTHONPATH=. oslo-config-generator --config-file=inventory/config-generator.conf

%install
echo "Start inventory install"
export PBR_VERSION=%{version}
%{__python} setup.py install --root=%{buildroot} \
                             --install-lib=%{pythonroot} \
                             --prefix=/usr \
                             --install-data=/usr/share \
                             --single-version-externally-managed

install -d -m 755 %{buildroot}%{local_etc_goenabledd}
install -p -D -m 755 etc/inventory/inventory_goenabled_check.sh %{buildroot}%{local_etc_goenabledd}/inventory_goenabled_check.sh

install -d -m 755 %{buildroot}%{local_etc_inventory}
install -p -D -m 755 etc/inventory/policy.json %{buildroot}%{local_etc_inventory}/policy.json

install -d -m 755 %{buildroot}%{local_etc_motdd}
install -p -D -m 755 etc/inventory/motd-system %{buildroot}%{local_etc_motdd}/10-system-config

install -m 755 -p -D scripts/inventory-api %{buildroot}/usr/lib/ocf/resource.d/platform/inventory-api
install -m 755 -p -D scripts/inventory-conductor %{buildroot}/usr/lib/ocf/resource.d/platform/inventory-conductor

install -m 644 -p -D scripts/inventory-api.service %{buildroot}%{_unitdir}/inventory-api.service
install -m 644 -p -D scripts/inventory-conductor.service %{buildroot}%{_unitdir}/inventory-conductor.service

# TODO(jkung) activate  inventory-agent with puppet integration)
# install -d -m 755 %{buildroot}%{local_etc_initd}
# install -p -D -m 755 scripts/inventory-agent-initd %{buildroot}%{local_etc_initd}/inventory-agent

# install -d -m 755 %{buildroot}%{local_etc_pmond}
# install -p -D -m 644 etc/inventory/inventory-agent-pmond.conf %{buildroot}%{local_etc_pmond}/inventory-agent-pmond.conf
# install -p -D -m 644 scripts/inventory-agent.service %{buildroot}%{_unitdir}/inventory-agent.service

# Install sql migration
install -m 644 inventory/db/sqlalchemy/migrate_repo/migrate.cfg %{buildroot}%{pythonroot}/inventory/db/sqlalchemy/migrate_repo/migrate.cfg

# install default config files
cd %{_builddir}/%{name}-%{version} && oslo-config-generator --config-file inventory/config-generator.conf --output-file %{_builddir}/%{name}-%{version}/inventory.conf.sample
# install -p -D -m 644 %{_builddir}/%{name}-%{version}/inventory.conf.sample %{buildroot}%{_sysconfdir}/inventory/inventory.conf


# TODO(jkung) activate  inventory-agent
# %post
# /usr/bin/systemctl enable inventory-agent.service >/dev/null 2>&1


%clean
echo "CLEAN CALLED"
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%doc LICENSE

%{local_bindir}/*

%{pythonroot}/%{name}

%{pythonroot}/%{name}-%{version}*.egg-info

%{local_etc_goenabledd}/*

%{local_etc_inventory}/*

%{local_etc_motdd}/*

# SM OCF Start/Stop/Monitor Scripts
%{ocf_resourced}/platform/inventory-api
%{ocf_resourced}/platform/inventory-conductor

# systemctl service files
%{_unitdir}/inventory-api.service
%{_unitdir}/inventory-conductor.service

# %{_bindir}/inventory-agent
%{_bindir}/inventory-api
%{_bindir}/inventory-conductor
%{_bindir}/inventory-dbsync
%{_bindir}/inventory-dnsmasq-lease-update

# inventory-agent files
# %{local_etc_initd}/inventory-agent
# %{local_etc_pmond}/inventory-agent-pmond.conf
# %{_unitdir}/inventory-agent.service
