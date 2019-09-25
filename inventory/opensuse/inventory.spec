Summary: StarlingX Inventory
Name: inventory
Version: 1.0.0
Release: 1
License: Apache-2.0
Group: System/Base
URL: https://www.starlingx.io
Source0: %{name}-%{version}.tar.gz

BuildRequires:  cgts-client
BuildRequires:  python-setuptools
BuildRequires:  python-jsonpatch
BuildRequires:  python-keystoneauth1
BuildRequires:  python-keystonemiddleware
BuildRequires:  python-mock
BuildRequires:  python-neutronclient
BuildRequires:  python2-oslo.concurrency
BuildRequires:  python2-oslo.config
BuildRequires:  python2-oslo.context
BuildRequires:  python2-oslo.db
BuildRequires:  python2-oslo.i18n
BuildRequires:  python2-oslo.log
BuildRequires:  python2-oslo.messaging
BuildRequires:  python2-oslo.middleware
BuildRequires:  python2-oslo.policy
BuildRequires:  python2-oslo.rootwrap
BuildRequires:  python2-oslo.serialization
BuildRequires:  python2-oslo.service
BuildRequires:  python2-oslo.utils
BuildRequires:  python2-oslo.versionedobjects
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
BuildRequires:  python2-WSME
BuildRequires:  systemd
BuildRequires:  systemd-devel
BuildRequires:  fdupes

Requires:   python-pyudev
Requires:   python-parted
Requires:   python-ipaddr
Requires:   python-paste
Requires:   python-eventlet
Requires:   python-futurist
Requires:   python-jsonpatch
Requires:   python-keystoneauth1
Requires:   python-keystonemiddleware
Requires:   python-neutronclient
Requires:   python2-oslo.concurrency
Requires:   python2-oslo.config
Requires:   python2-oslo.context
Requires:   python2-oslo.db
Requires:   python2-oslo.i18n
Requires:   python2-oslo.log
Requires:   python2-oslo.messaging
Requires:   python2-oslo.middleware
Requires:   python2-oslo.policy
Requires:   python2-oslo.rootwrap
Requires:   python2-oslo.serialization
Requires:   python2-oslo.service
Requires:   python2-oslo.utils
Requires:   python2-oslo.versionedobjects
Requires:   python2-osprofiler
Requires:   python-pbr
Requires:   python-pecan
Requires:   python-psutil
Requires:   python-requests
Requires:   python-retrying
Requires:   python-six
Requires:   python-sqlalchemy
Requires:   python-stevedore
Requires:   python-webob
Requires:   python2-WSME
Requires:   tsconfig

%description
The inventory service for StarlingX

%define local_etc_goenabledd %{_sysconfdir}/goenabled.d/
%define local_etc_inventory  %{_sysconfdir}/inventory/
%define local_etc_motdd      %{_sysconfdir}/motd.d/
%define pythonroot           %{_libdir}/python2.7/site-packages
%define ocf_resourced        %{_libdir}/ocf/resource.d

%define local_etc_initd %{_sysconfdir}/init.d/
%define local_etc_pmond %{_sysconfdir}/pmon.d/

%define debug_package %{nil}

%prep
%setup -n %{name}-%{version}/%{name}

# Remove bundled egg-info
rm -rf *.egg-info

%build
export PBR_VERSION=%{version}
%{__python} setup.py build
PYTHONPATH=. oslo-config-generator --config-file=inventory/config-generator.conf

%install
export PBR_VERSION=%{version}
%{__python} setup.py install --root=%{buildroot} \
                             --install-lib=%{pythonroot} \
                             --prefix=/usr \
                             --install-data=/usr/share \
                             --single-version-externally-managed

install -d -m 755 %{buildroot}%{local_etc_goenabledd}
install -p -D -m 755 etc/inventory/inventory_goenabled_check.sh %{buildroot}%{local_etc_goenabledd}/inventory_goenabled_check.sh

install -d -m 755 %{buildroot}%{local_etc_inventory}
install -p -D -m 644 etc/inventory/policy.json %{buildroot}%{local_etc_inventory}/policy.json

install -d -m 755 %{buildroot}%{local_etc_motdd}
install -p -D -m 755 etc/inventory/motd-system %{buildroot}%{local_etc_motdd}/10-system-config

install -m 755 -p -D scripts/inventory-api %{buildroot}%{_libdir}/ocf/resource.d/platform/inventory-api
install -m 755 -p -D scripts/inventory-conductor %{buildroot}%{_libdir}/ocf/resource.d/platform/inventory-conductor

install -m 644 -p -D scripts/inventory-api.service %{buildroot}%{_unitdir}/inventory-api.service
install -m 644 -p -D scripts/inventory-conductor.service %{buildroot}%{_unitdir}/inventory-conductor.service

# Install sql migration
install -m 644 inventory/db/sqlalchemy/migrate_repo/migrate.cfg %{buildroot}%{pythonroot}/inventory/db/sqlalchemy/migrate_repo/migrate.cfg
%fdupes %{buildroot}%{pythonroot}/inventory-1.0-py2.7.egg-info/

%pre
%service_add_pre inventory-api.service
%service_add_pre inventory-conductor.service

%post
%service_add_post inventory-api.service
%service_add_post inventory-conductor.service
# TODO(jkung) activate  inventory-agent
# /usr/bin/systemctl enable inventory-agent.service >/dev/null 2>&1

%preun
%service_del_preun inventory-api.service
%service_del_preun inventory-conductor.service

%postun
%service_del_postun inventory-api.service
%service_del_postun inventory-conductor.service


%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc LICENSE

%{_bindir}/*
%{pythonroot}/%{name}
%{pythonroot}/%{name}-%{version}*.egg-info
%dir %{local_etc_goenabledd}
%dir %{local_etc_inventory}
%dir %{local_etc_motdd}
%dir %{_libdir}/ocf
%dir %{_libdir}/ocf/resource.d
%dir %{_libdir}/ocf/resource.d/platform
%config %{local_etc_inventory}/policy.json
%{local_etc_goenabledd}/*
%{local_etc_motdd}/*

# SM OCF Start/Stop/Monitor Scripts
%{ocf_resourced}/platform/inventory-api
%{ocf_resourced}/platform/inventory-conductor

# systemctl service files
%{_unitdir}/inventory-api.service
%{_unitdir}/inventory-conductor.service

%{_bindir}/inventory-api
%{_bindir}/inventory-conductor
%{_bindir}/inventory-dbsync
%{_bindir}/inventory-dnsmasq-lease-update

%changelog
