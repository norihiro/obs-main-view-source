Name: obs-studio-plugin-main-view-source
Version: @VERSION@
Release: @RELEASE@%{?dist}
Summary: A source plugin to duplicate the main view on OBS Studio
License: GPLv2+

Source0: %{name}-%{version}.tar.bz2
BuildRequires: cmake, gcc, gcc-c++
BuildRequires: obs-studio-devel

%description
This plugin provide a video source that duplicate the main view on OBS Studio.
This plugin is compatible with Source Record filter or Dedicated NDI filter.

%prep
%autosetup -p1
sed -i -e 's/project(obs-/project(/g' CMakeLists.txt

%build
%{cmake} -DLINUX_PORTABLE=OFF -DLINUX_RPATH=OFF -DQT_VERSION=6
%{cmake_build}

%install
%{cmake_install}

%files
%{_libdir}/obs-plugins/*.so
%{_datadir}/obs/obs-plugins/*/
%license LICENSE
