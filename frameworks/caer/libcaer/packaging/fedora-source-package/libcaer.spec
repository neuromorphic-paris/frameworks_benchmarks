Summary: Minimal C library to interact with neuromorphic sensors and processors
Name:    libcaer
Version: 3.1.0
Release: 0%{?dist}
License: BSD
URL:     https://gitlab.com/inivation/libcaer/
Vendor:  iniVation AG

Source0: https://gitlab.com/inivation/%{name}/-/archive/%{version}/%{name}-%{version}.tar.gz

BuildRequires: gcc >= 4.9, gcc-c++ >= 4.9, cmake >= 2.8.12, pkgconfig >= 0.29.0, libusbx-devel >= 1.0.17, libserialport-devel >= 0.1.1, opencv-devel >= 3.1.0
Requires: libusbx >= 1.0.17, libserialport >= 0.1.1, opencv >= 3.1.0

%description
Minimal C library to access, configure and get data from neuromorphic sensors
and processors. Currently supported devices are the Dynamic Vision Sensor
(DVS), the DAVIS cameras, and the Dynap-SE neuromorphic processor.

%package devel
Summary: Minimal C library to interact with neuromorphic sensors and processors (development files)
Requires: %{name}%{?_isa} = %{version}-%{release}, cmake >= 2.8.12, pkgconfig >= 0.29.0, libusbx-devel >= 1.0.17, libserialport-devel >= 0.1.1, opencv-devel >= 3.1.0

%description devel
Development files for libcaer, such as headers, pkg-config files, etc..

%package static
Summary: Minimal C library to interact with neuromorphic sensors and processors (static library)
Requires: %{name}-devel%{?_isa} = %{version}-%{release}

%description static
Static version (.a) of the libcaer library.

%prep
%autosetup

%build
%cmake -DENABLE_STATIC=1 -DUDEV_SELINUX=1 -DENABLE_SERIALDEV=1 -DENABLE_OPENCV=1 .
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}

%files
/lib/udev/rules.d/
%{_libdir}/libcaer.so.*

%files devel
%{_includedir}/libcaer/
%{_includedir}/libcaercpp/
%{_datarootdir}/caer/
%{_libdir}/libcaer.so
%{_libdir}/pkgconfig/
%{_libdir}/cmake/libcaer/

%files static
%{_libdir}/libcaer.a

%changelog
* Fri Mar 23 2018 iniVation AG <support@inivation.com>
See ChangeLog file in source or GitLab.
