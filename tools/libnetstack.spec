Name: libnetstack
Version: 0.7.0
Release: 1%{?dist}
Summary: Track addresses, interfaces, routes and neighbors
License: Apache-2.0
Source0: https://github.com/dankamongmen/libnetstack/archive/refs/tags/v0.7.0.tar.gz
URL: https://github.com/dankamongmen/libnetstack
BuildRequires: gcc
BuildRequires: make
BuildRequires: cmake

%description
Libnetstack, a small library for modeling the machine's networking.

%package devel
Summary: Development files for libnetstack
Requires: %{name}%{_isa} = %{version}-%{release}
Requires: pkgconfig

%description devel
Header files for libnetstack.

%prep
%autosetup

%build
%set_build_flags
./configure --prefix=%{_prefix} --libdir=/%{_libdir} --libdevdir=/%{_libdir} --mandir=%{_mandir} --includedir=%{_includedir}

%make_build

%install
%make_install

%files
%attr(0755,root,root) %{_libdir}/libnetstack.so.*
%license COPYING

%files devel
%{_includedir}/netstack.h
%{_libdir}/libnetstack.so
%exclude %{_libdir}/libnetstack.a
%{_libdir}/pkgconfig/*

%changelog
* Sun, 01 Jan 2023 nick black <nickblack@linux.com> - 0.7.0-1
- Initial version
