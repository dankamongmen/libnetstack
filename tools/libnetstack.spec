Name: libnetstack
Version: 0.7.3
Release: 1%{?dist}
Summary: Track addresses, interfaces, routes and neighbors
License: Apache-2.0
Source0: https://github.com/dankamongmen/libnetstack/archive/refs/tags/v%{version}.tar.gz
URL: https://github.com/dankamongmen/libnetstack
BuildRequires: gcc
BuildRequires: make
BuildRequires: cmake
BuildRequires: libnl3-devel

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

%define __cmake_in_source_build 1

%build
%cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON
%cmake_build

#%check
#ctest -V %{?_smp_mflags}

%install
%cmake_install

%files
%{_bindir}/netstack-demo
%{_libdir}/libnetstack.so.*
%license LICENSE

%files devel
%{_includedir}/netstack.h
%{_libdir}/libnetstack.so
%{_libdir}/libnetstack.a
%{_libdir}/pkgconfig/*
%{_libdir}/cmake/libnetstack

%changelog
* Tue May 23 2023 nick black <nickblack@linux.com> - 0.7.3-1
- Initial version
