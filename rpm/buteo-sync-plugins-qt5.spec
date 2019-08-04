Name: buteo-sync-plugins-qt5
Version: 0.8.29
Release: 1
Summary: Synchronization plugins
URL: https://git.sailfishos.org/mer-core/buteo-sync-plugins
License: LGPLv2.1
Source0: %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Contacts)
BuildRequires: pkgconfig(Qt5Versit)
BuildRequires: pkgconfig(Qt5Sql)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Test)
BuildRequires: pkgconfig(Qt5SystemInfo)
BuildRequires: pkgconfig(openobex)
BuildRequires: pkgconfig(accounts-qt5)
BuildRequires: pkgconfig(libsignon-qt5)
BuildRequires: pkgconfig(buteosyncml5)
BuildRequires: pkgconfig(buteosyncfw5) >= 0.10.0
BuildRequires: pkgconfig(qtcontacts-sqlite-qt5-extensions)
BuildRequires: pkgconfig(contactcache-qt5) >= 0.0.76
BuildRequires: pkgconfig(libmkcal-qt5)
BuildRequires: pkgconfig(KF5CalendarCore)
BuildRequires: pkgconfig(sqlite3)
BuildRequires: pkgconfig(KF5BluezQt)
BuildRequires: doxygen

%description
%{summary}.

%files
%defattr(-,root,root,-)
%config %{_sysconfdir}/buteo/xml/*.xml
%config %{_sysconfdir}/buteo/profiles/server/*.xml
%config %{_sysconfdir}/buteo/profiles/client/*.xml
%config %{_sysconfdir}/buteo/profiles/storage/*.xml
%config %{_sysconfdir}/buteo/profiles/service/*.xml
%config %{_sysconfdir}/buteo/profiles/sync/bt_template.xml
%config %{_sysconfdir}/buteo/plugins/syncmlserver/*.xml
%{_libdir}/buteo-plugins-qt5/oopp/*
%{_libdir}/buteo-plugins-qt5/*.so
%{_libdir}/*.so.*

%package devel
Requires: %{name} = %{version}-%{release}
Summary: Development files for %{name}

%description devel
%{summary}.

%files devel
%defattr(-,root,root,-)
%{_includedir}/*
%{_libdir}/*.so
%{_libdir}/*.prl
%{_libdir}/pkgconfig/*.pc

%package doc
Summary: Documentation for %{name}

%description doc
%{summary}.

%files doc
%defattr(-,root,root,-)
%{_docdir}/buteo-sync-plugins-doc


%package tests
Summary: Tests for %{name}
Requires: %{name} = %{version}-%{release}
Requires: blts-tools

%description tests
%{summary}.

%files tests
%defattr(-,root,root,-)
/opt/tests/buteo-sync-plugins


%package -n buteo-service-memotoo
Summary: Memotoo service description for Buteo SyncML
Requires: %{name} = %{version}

%description -n buteo-service-memotoo
%{summary}.

%files -n buteo-service-memotoo
%defattr(-,root,root,-)
%config %{_sysconfdir}/buteo/profiles/sync/memotoo.com.xml

%prep
%setup -q -n %{name}-%{version}


%build
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}
make doc %{_smp_mflags}


%install
make INSTALL_ROOT=%{buildroot} install
rm -f %{buildroot}/%{_sysconfdir}/buteo/profiles/sync/switch.xml


%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig
