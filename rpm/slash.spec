%global debug_package %{nil}

Name:           slash
Version:        %{_version}
Release:        1%{?dist}
Summary:        SLASH/VRT System
License:        MIT
URL:            https://github.com/Xilinx/SLASH

Source0:        %{name}-%{version}.tar.gz

BuildRequires:  bash
BuildRequires:  cmake
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  inih-devel
BuildRequires:  jsoncpp-devel
BuildRequires:  systemd-devel
BuildRequires:  libxml2-devel
BuildRequires:  zeromq-devel
BuildRequires:  ninja-build
BuildRequires:  pkg-config
BuildRequires:  rsync
BuildRequires:  zlib-devel
BuildRequires:  systemd-rpm-macros

# ---- Metapackages ----

%description
SLASH/VRT System Full

%package        slash-devel
Summary:        SLASH/VRT System Full (development files)
Requires:       slash-sim-emu-dev = %{version}-%{release}
Requires:       libslash-devel = %{version}-%{release}
Requires:       libvrtd-devel = %{version}-%{release}
BuildArch:      noarch

%description    slash-devel
SLASH/VRT System Full (development files)

%package        slash-sim-emu
Summary:        SLASH/VRT System for simulation and emulation
Requires:       libvrt = %{version}-%{release}
BuildArch:      noarch

%description    slash-sim-emu
SLASH/VRT System for simulation and emulation

%package        slash-sim-emu-devel
Summary:        SLASH/VRT System for simulation and emulation (development files)
Requires:       slash-sim-emu = %{version}-%{release}
Requires:       v80++ = %{version}-%{release}
Requires:       libvrt-devel = %{version}-%{release}
BuildArch:      noarch

%description    slash-sim-emu-devel
SLASH/VRT System for simulation and emulation (development files)

%package -n     slash-dkms
Summary:        SLASH kernel module (DKMS)
Requires:       dkms
BuildArch:      noarch

%description -n slash-dkms
SLASH kernel module (DKMS)

# ---- Libraries ----

%package -n     libslash
Summary:        Library for interacting with the SLASH kernel module

%description -n libslash
Library for interacting with the SLASH kernel module

%package -n     libslash-devel
Summary:        Library for interacting with the SLASH kernel module (development files)
Requires:       libslash = %{version}-%{release}

%description -n libslash-devel
Library for interacting with the SLASH kernel module (development files)

%package -n     vrtd
Summary:        VRTd daemon for managing VRT devices
Requires:       libslash = %{version}-%{release}
%{?systemd_requires}

%description -n vrtd
VRTd daemon for managing VRT devices

%package -n     libvrtd
Summary:        Library for interacting with the VRTd daemon
Requires:       libslash = %{version}-%{release}

%description -n libvrtd
Library for interacting with the VRTd daemon for managing VRT devices

%package -n     libvrtd-devel
Summary:        Library for interacting with the VRTd daemon (development files)
Requires:       libvrtd = %{version}-%{release}
Requires:       libslash-devel = %{version}-%{release}

%description -n libvrtd-devel
Library for interacting with the VRTd daemon for managing VRT devices (development files)

%package -n     libvrt
Summary:        VRT Runtime
Requires:       libvrtd = %{version}-%{release}
Requires:       systemd

%description -n libvrt
VRT Runtime

%package -n     libvrt-devel
Summary:        VRT Runtime (development files)
Requires:       libvrt = %{version}-%{release}
Requires:       libvrtd-devel = %{version}-%{release}
Requires:       jsoncpp-devel
Requires:       libxml2-devel
Requires:       zeromq-devel
Requires:       zlib-devel

%description -n libvrt-devel
VRT Runtime (development files)

%package -n     v80++
Summary:        SLASH Linker
Requires:       python3
BuildArch:      noarch

%description -n v80++
SLASH Linker

# ---- Build ----

%prep
%autosetup -n %{name}-%{version}

%build
# Adapt cmake paths for RPM (no multiarch on RPM distros)
cmake -B pbuild/vrt -S vrt -G Ninja \
    -DVRT_INCLUDE_VRTD=ON \
    -DVRTD_INCLUDE_LIBSLASH=ON \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_BINDIR=%{_bindir} \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
    -DCMAKE_INSTALL_LOCALSTATEDIR=%{_localstatedir} \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build pbuild/vrt

%install
DESTDIR=%{buildroot} cmake --build pbuild/vrt --target install

# Install systemd units
install -D -m 0644 vrt/vrtd/systemd/vrtd.service %{buildroot}%{_unitdir}/vrtd.service
install -D -m 0644 vrt/vrtd/systemd/vrtd.socket  %{buildroot}%{_unitdir}/vrtd.socket

# Install udev rules
install -D -m 0644 vrt/vrtd/udev/99-vrtd.rules %{buildroot}%{_udevrulesdir}/99-vrtd.rules

# Install the linker
mkdir -p %{buildroot}%{_prefix}/lib/slash/linker
rsync --delete -a linker/ %{buildroot}%{_prefix}/lib/slash/linker/
mkdir -p %{buildroot}%{_bindir}
cat <<'EOFBIN' > %{buildroot}%{_bindir}/v80++
#!/bin/sh
python3 /usr/lib/slash/linker/src/main.py
EOFBIN
chmod 0755 %{buildroot}%{_bindir}/v80++

# ---- File lists ----
# You must list every file each subpackage owns.
# Adjust these globs to match your actual installed files.

%files
# metapackage — empty

%files dev
# metapackage — empty

%files sim-emu
# metapackage — empty

%files sim-emu-dev
# metapackage — empty

%files -n slash-dkms
# TODO: add your DKMS source tree, e.g.:
# %{_prefix}/src/%{name}-%{version}/

%files -n libslash
%{_libdir}/libslash.so.*

%files -n libslash-devel
%{_libdir}/libslash.so
%{_includedir}/slash/
%{_libdir}/pkgconfig/libslash.pc
%{_libdir}/cmake/libslash/

%files -n vrtd
%{_bindir}/vrtd
%{_unitdir}/vrtd.service
%{_unitdir}/vrtd.socket
%{_udevrulesdir}/99-vrtd.rules

%files -n libvrtd
%{_libdir}/libvrtd.so.*

%files -n libvrtd-devel
%{_libdir}/libvrtd.so
%{_includedir}/vrtd/
%{_libdir}/pkgconfig/libvrtd.pc
%{_libdir}/cmake/libvrtd/

%files -n libvrt
%{_libdir}/libvrt.so.*

%files -n libvrt-devel
%{_libdir}/libvrt.so
%{_includedir}/vrt/
%{_libdir}/pkgconfig/libvrt.pc
%{_libdir}/cmake/libvrt/

%files -n v80++
%{_bindir}/v80++
%{_prefix}/lib/slash/linker/

# ---- Scriptlets ----

%post -n libslash -p /sbin/ldconfig
%postun -n libslash -p /sbin/ldconfig

%post -n libvrtd -p /sbin/ldconfig
%postun -n libvrtd -p /sbin/ldconfig

%post -n libvrt -p /sbin/ldconfig
%postun -n libvrt -p /sbin/ldconfig

%post -n vrtd
%systemd_post vrtd.service vrtd.socket

%preun -n vrtd
%systemd_preun vrtd.service vrtd.socket

%postun -n vrtd
%systemd_postun_with_restart vrtd.service vrtd.socket

%changelog
* Thu Jun 12 2025 Vlad-Gabriel Serbu <Vlad-Gabriel.Serbu@amd.com> - %{_version}-1
- Initial RPM packaging
