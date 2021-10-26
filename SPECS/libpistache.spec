# SPDX-FileCopyrightText: 2021 Hugo Rodrigues
#
# SPDX-License-Identifier: Apache-2.0

%define github_owner pistacheio
%define github_repo  pistache

Name:           libpistache
Version:        0.0.3
Release:        1%{?dist}
Summary:        A high-performance REST Toolkit written in C++ 
URL:            http://pistache.io/

Source0: https://github.com/%{github_owner}/%{github_repo}/archive/%{version}/%{github_repo}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  gcc-c++
BuildRequires:  coreutils


# Checks for what distro we are building. It defaults to EL/Fedora
%if 0%{?suse_version}
License:        Apache-2.0
BuildRequires:  gtest
# SUSE doesn't have builddep
BuildRequires:  rapidjson-devel
BuildRequires:  libopenssl-devel
BuildRequires:  libcurl-devel
%else
License:        ASL 2.0
BuildRequires:  gtest-devel
BuildRequires:  pkgconfig(RapidJSON)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(libcurl)
%endif

Requires:       openssl

%description
Pistache is a modern and elegant HTTP and REST framework for C++. It is entirely written in pure-C++17 and provides a clear and pleasant API.

%package devel
Summary: Development files for %{name}
Requires: %{name}%{?_isa} = %{?epoch:%{epoch}:}%{version}-%{release}

%description devel
Development files for %{name}.

%package        examples
Summary:        Programming examples for %{name}
Requires:       %{name}-devel = %{version}-%{release}
BuildArch:      noarch

%description    examples
The %{name}-examples package contains example source files for %{name}.

%prep
%autosetup -n %{github_repo}-%{version}

%build
%meson -DPISTACHE_USE_SSL=true -DPISTACHE_BUILD_EXAMPLES=false -DPISTACHE_BUILD_TESTS=true -DPISTACHE_BUILD_DOCS=false
%meson_build

%install
%meson_install
install -Dm 644 examples/*cc -t $RPM_BUILD_ROOT%{_docdir}/%{name}/examples/
install -Dm 644 README.md -t $RPM_BUILD_ROOT%{_docdir}/%{name}/
install -Dm 644 LICENSE -t $RPM_BUILD_ROOT%{_docdir}/%{name}/

%check
%meson_test

%files
%{_libdir}/%{name}.so.*

%files devel
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/README.md
%{_includedir}/pistache/
%{_libdir}/%{name}.so
%{_libdir}/pkgconfig/%{name}.pc

%files examples
%{_docdir}/%{name}/examples/*.cc

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%changelog
* Fri Oct 22 2021 Hugo Rodrigues <hugorodrigues@hugorodrigues.xyz>
- Initial RPM package
