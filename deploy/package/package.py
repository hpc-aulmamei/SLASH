#!/usr/bin/env python3

# ##################################################################################################
#  The MIT License (MIT)
#  Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
# 
#  Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#  and associated documentation files (the "Software"), to deal in the Software without restriction,
#  including without limitation the rights to use, copy, modify, merge, publish, distribute,
#  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
# 
#  The above copyright notice and this permission notice shall be included in all copies or
#  substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
# NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
# ##################################################################################################

import os
import sys
import subprocess
import shutil
import argparse
from datetime import datetime

PACKAGE_NAME = "amd-vrt"
MAINTAINER = "AMD <support@amd.com>"
DEB_ARCH = "amd64"
RPM_ARCH = "x86_64"
DESCRIPTION = "AMD V80 Runtime API, SMI and PCIe driver package"

# Debian-like dependencies (as given)
DEB_DEPENDS = "libxml2, libzmq3-dev, libjsoncpp-dev"

# Rough RPM equivalents (adjust if needed in your environment)
RPM_REQUIRES = [
    "libxml2",
    "czmq",            # if you actually use libzmq directly, use 'zeromq' or 'zeromq-libs'
    "jsoncpp",
]

def run_command(cmd, cwd=None, env=None):
    try:
        result = subprocess.run(
            cmd, shell=True, check=True, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd, env=env
        )
        return result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {cmd}")
        print(f"STDERR:\n{e.stderr}")
        sys.exit(1)

def get_version_from_header(repo_root):
    """Extract version from vrt_version.hpp header file"""
    version_file = os.path.join(repo_root, "vrt", "include", "api", "vrt_version.hpp")
    if not os.path.exists(version_file):
        print(f"Warning: Version file not found at {version_file}")
        return "1.0.0"

    major = "1"; minor = "0"; patch = "0"; git_tag = ""
    try:
        with open(version_file, "r") as f:
            content = f.read()
            import re
            m = re.search(r'#define\s+VRT_VERSION_MAJOR\s+(\d+)', content);  major = m.group(1) if m else major
            m = re.search(r'#define\s+VRT_VERSION_MINOR\s+(\d+)', content);  minor = m.group(1) if m else minor
            m = re.search(r'#define\s+VRT_VERSION_PATCH\s+(\d+)', content);  patch = m.group(1) if m else patch
            m = re.search(r'#define\s+GIT_TAG\s+"([^"]+)"', content);         git_tag = m.group(1) if m else ""

        version = f"{major}.{minor}.{patch}"
        if version == "1.0.0" and git_tag.startswith("v"):
            version = git_tag[1:]
            print(f"Using version from GIT_TAG: {version}")
        else:
            print(f"Extracted version from components: {version}")
        return version
    except Exception as e:
        print(f"Error extracting version from header file: {e}")
        return "1.0.0"

def detect_packaging_format(forced=None):
    """
    Return 'deb' or 'rpm'.
    If --format is provided, honor it. Otherwise detect via /etc/os-release.
    """
    if forced:
        return forced
    os_release = "/etc/os-release"
    if os.path.exists(os_release):
        data = open(os_release).read().lower()
        if any(k in data for k in ["ubuntu", "debian"]):
            return "deb"
        if any(k in data for k in ["rocky", "rhel", "red hat", "centos", "almalinux", "fedora"]):
            return "rpm"
    # Fallback: try tools
    if shutil.which("dpkg-deb"):
        return "deb"
    if shutil.which("rpmbuild"):
        return "rpm"
    print("Could not detect packaging system. Install dpkg-deb or rpmbuild, or pass --format deb|rpm.")
    sys.exit(2)

def create_stage_tree(repo_root):
    """Create the staging directory with the final filesystem layout"""
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    out_dir = os.path.join(repo_root, "deploy", "output")
    os.makedirs(out_dir, exist_ok=True)
    stage_dir = os.path.join(out_dir, f"{PACKAGE_NAME}-stage-{timestamp}")
    os.makedirs(stage_dir, exist_ok=True)

    # Create common dirs
    for d in [
        "usr/local/bin",
        "usr/local/lib",
        "usr/local/vrt/include",
        "usr/src/pcie-hotplug-drv",
        "opt/amd/vrt",
    ]:
        os.makedirs(os.path.join(stage_dir, d), exist_ok=True)

    return stage_dir

def copy_design_pdi(repo_root, stage_dir):
    pdi_src = os.path.join(repo_root, "deploy", "design.pdi")
    pdi_dst = os.path.join(stage_dir, "opt/amd/vrt/design.pdi")
    if os.path.exists(pdi_src):
        shutil.copy2(pdi_src, pdi_dst)
        print("design.pdi copied to package")
    else:
        print(f"Warning: design.pdi not found at {pdi_src}")

def build_and_copy_vrt(repo_root, stage_dir):
    vrt_dir = os.path.join(repo_root, "vrt")
    build_dir = os.path.join(vrt_dir, "build")
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir, exist_ok=True)
    run_command("cmake ..", cwd=build_dir)
    run_command("make -j$(nproc)", cwd=build_dir)

    lib_dir = os.path.join(build_dir, "lib")
    if os.path.isdir(lib_dir):
        for lib_file in os.listdir(lib_dir):
            if lib_file.startswith("libvrt") and (lib_file.endswith(".so") or lib_file.endswith(".a")):
                shutil.copy2(os.path.join(lib_dir, lib_file), os.path.join(stage_dir, "usr/local/lib", lib_file))

    include_src = os.path.join(vrt_dir, "include")
    include_dst = os.path.join(stage_dir, "usr/local/vrt/include")
    if os.path.exists(include_src):
        for root, _, files in os.walk(include_src):
            for file in files:
                if file.endswith((".h", ".hpp")):
                    rel = os.path.relpath(root, include_src)
                    dst_dir = os.path.join(include_dst, rel)
                    os.makedirs(dst_dir, exist_ok=True)
                    shutil.copy2(os.path.join(root, file), os.path.join(dst_dir, file))

    scripts_src = os.path.join(vrt_dir, "scripts")
    if os.path.exists(scripts_src):
        scripts_dst = os.path.join(stage_dir, "usr/local/vrt")
        os.makedirs(scripts_dst, exist_ok=True)
        for item in os.listdir(scripts_src):
            s = os.path.join(scripts_src, item)
            d = os.path.join(scripts_dst, item)
            if os.path.isfile(s):
                shutil.copy2(s, d)
                # make script executable if it looks like one
                if s.endswith((".sh", ".py")) or os.access(s, os.X_OK):
                    os.chmod(d, 0o755)

    print("VRT API built and files copied to stage")

def build_and_copy_smi(repo_root, stage_dir):
    smi_dir = os.path.join(repo_root, "smi")
    build_dir = os.path.join(smi_dir, "build")
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
    os.makedirs(build_dir, exist_ok=True)
    run_command("cmake ..", cwd=build_dir)
    run_command("make -j$(nproc)", cwd=build_dir)

    # find <something>-smi binaries (incl. v80-smi)
    for root, _, files in os.walk(build_dir):
        for f in files:
            full = os.path.join(root, f)
            if (f == "v80-smi" or f.endswith("-smi")) and os.access(full, os.X_OK):
                print(f"Found SMI binary: {f}")
                dst = os.path.join(stage_dir, "usr/local/bin", f)
                shutil.copy2(full, dst)
                os.chmod(dst, 0o755)

    print("SMI CLI built and files copied to stage")

def copy_pcie_driver(repo_root, stage_dir):
    src = os.path.join(repo_root, "submodules/pcie-hotplug-drv")
    dst = os.path.join(stage_dir, "usr/src/pcie-hotplug-drv")
    if not os.path.exists(src):
        print(f"Warning: PCIe driver directory not found at {src}")
        return
    for item in os.listdir(src):
        s = os.path.join(src, item); d = os.path.join(dst, item)
        if os.path.isdir(s):
            shutil.copytree(s, d, symlinks=True)
        else:
            shutil.copy2(s, d)
    print("PCIe hotplug driver source copied to stage")

# ----------------------- DEB PACKAGING -----------------------

def write_debian_scripts(debian_dir):
    os.makedirs(debian_dir, exist_ok=True)
    postinst = """#!/bin/bash
set -e
echo "/usr/local/lib" > /etc/ld.so.conf.d/amd-vrt.conf
ldconfig

if [ -d "/usr/src/pcie-hotplug-drv" ]; then
    echo "Building PCIe hotplug driver..."
    cd /usr/src/pcie-hotplug-drv
    make clean || true
    make || true
    make install || true

    echo "Configuring pcie_hotplug module to load at boot..."
    echo "pcie_hotplug" > /etc/modules-load.d/amd-vrt.conf

    echo "Creating VRT device permission rules..."
    cat > /etc/udev/rules.d/99-amd-vrt-permissions.rules << 'EOF'
KERNEL=="pcie_hotplug", MODE="0666", GROUP="users"
KERNEL=="pcie_hotplug*", MODE="0666", GROUP="users"
EOF
    udevadm control --reload-rules

    cat > /usr/local/bin/vrt-setup-devices.sh << 'EOF'
#!/bin/bash
if ! lsmod | grep -q "pcie_hotplug"; then
    modprobe pcie_hotplug || true
    sleep 1
fi
for dev in /dev/pcie_hotplug*; do
  if [ -e "$dev" ]; then
    chmod 666 "$dev" || true
    chown root:users "$dev" || true
  fi
done
EOF
    chmod +x /usr/local/bin/vrt-setup-devices.sh

    cat > /etc/systemd/system/vrt-devices.service << 'EOF'
[Unit]
Description=VRT Device Permissions
After=systemd-udev-settle.service
After=systemd-modules-load.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/vrt-setup-devices.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    systemctl daemon-reload
    systemctl enable vrt-devices.service || true

    if lsmod | grep -q "pcie_hotplug"; then
        rmmod pcie_hotplug || true
    fi
    modprobe pcie_hotplug || true
    udevadm trigger || true
    sleep 1
    /usr/local/bin/vrt-setup-devices.sh || true
fi
exit 0
"""
    prerm = """#!/bin/bash
set -e
if lsmod | grep -q "pcie_hotplug"; then
    rmmod pcie_hotplug || true
fi
exit 0
"""
    postrm = """#!/bin/bash
set -e
rm -f /etc/modules-load.d/amd-vrt.conf || true
if [ -f "/etc/udev/rules.d/99-amd-vrt-permissions.rules" ]; then
    rm -f /etc/udev/rules.d/99-amd-vrt-permissions.rules
    udevadm control --reload-rules || true
fi
if [ -f "/etc/systemd/system/vrt-devices.service" ]; then
    systemctl disable vrt-devices.service || true
    systemctl stop vrt-devices.service || true
    rm -f /etc/systemd/system/vrt-devices.service
    systemctl daemon-reload || true
fi
rm -f /usr/local/bin/vrt-setup-devices.sh || true
if [ -f "/etc/ld.so.conf.d/amd-vrt.conf" ]; then
    rm -f /etc/ld.so.conf.d/amd-vrt.conf
    ldconfig
fi
exit 0
"""
    with open(os.path.join(debian_dir, "postinst"), "w") as f: f.write(postinst)
    with open(os.path.join(debian_dir, "prerm"), "w") as f: f.write(prerm)
    with open(os.path.join(debian_dir, "postrm"), "w") as f: f.write(postrm)
    os.chmod(os.path.join(debian_dir, "postinst"), 0o755)
    os.chmod(os.path.join(debian_dir, "prerm"), 0o755)
    os.chmod(os.path.join(debian_dir, "postrm"), 0o755)

def build_deb(stage_dir, version, repo_root):
    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    deb_root = stage_dir  # stage already mirrors FS
    debian_dir = os.path.join(deb_root, "DEBIAN")
    os.makedirs(debian_dir, exist_ok=True)

    control = f"""Package: {PACKAGE_NAME}
Version: {version}
Architecture: {DEB_ARCH}
Maintainer: {MAINTAINER}
Depends: {DEB_DEPENDS}
Section: utils
Priority: optional
Homepage: https://www.amd.com/
Description: {DESCRIPTION}
 This package includes:
  * VRT API - Runtime API for AMD V80 acceleration
  * SMI CLI - System Management Interface command-line utility
  * PCIe hotplug driver - Driver for PCIe hotplug functionality
"""
    with open(os.path.join(debian_dir, "control"), "w") as f:
        f.write(control)

    write_debian_scripts(debian_dir)

    out_dir = os.path.join(repo_root, "deploy", "output")
    os.makedirs(out_dir, exist_ok=True)
    deb_name = f"{PACKAGE_NAME}_{version}_{timestamp}_{DEB_ARCH}.deb"
    deb_path = os.path.join(out_dir, deb_name)

    run_command(f"dpkg-deb --build --root-owner-group {deb_root} {deb_path}")
    print(f"DEB created: {deb_path}")
    return deb_path

# ----------------------- RPM PACKAGING -----------------------

def rpm_topdirs(base_out):
    top = os.path.join(base_out, "rpmbuild")
    dirs = {
        "TOP": top,
        "BUILD": os.path.join(top, "BUILD"),
        "RPMS": os.path.join(top, "RPMS"),
        "SOURCES": os.path.join(top, "SOURCES"),
        "SPECS": os.path.join(top, "SPECS"),
        "SRPMS": os.path.join(top, "SRPMS"),
    }
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)
    return dirs

def make_rpm_spec(spec_path, version, release, stage_dir):
    """
    Create a SPEC that copies pre-built files from stage_dir into %{buildroot}.
    Scriptlets mirror the Debian postinst/prerm/postrm.
    """
    summary = DESCRIPTION
    license_str = "MIT"
    url = "https://www.amd.com/"
    requires = "\n".join([f"Requires: {r}" for r in RPM_REQUIRES])

    # Scriptlets (no shebangs)
    post = r'''
echo "/usr/local/lib" > /etc/ld.so.conf.d/amd-vrt.conf
/sbin/ldconfig

if [ -d "/usr/src/pcie-hotplug-drv" ]; then
    echo "Building PCIe hotplug driver..."
    cd /usr/src/pcie-hotplug-drv
    make clean || true
    make || true
    make install || true

    echo "pcie_hotplug" > /etc/modules-load.d/amd-vrt.conf

    cat > /etc/udev/rules.d/99-amd-vrt-permissions.rules << 'EOF'
KERNEL=="pcie_hotplug", MODE="0666", GROUP="users"
KERNEL=="pcie_hotplug*", MODE="0666", GROUP="users"
EOF
    /usr/bin/udevadm control --reload-rules || true

    cat > /usr/local/bin/vrt-setup-devices.sh << 'EOF'
#!/bin/bash
if ! /usr/sbin/lsmod | /usr/bin/grep -q "pcie_hotplug"; then
    /usr/sbin/modprobe pcie_hotplug || true
    /usr/bin/sleep 1
fi
for dev in /dev/pcie_hotplug*; do
  if [ -e "$dev" ]; then
    /usr/bin/chmod 666 "$dev" || true
    /usr/bin/chown root:users "$dev" || true
  fi
done
EOF
    /usr/bin/chmod +x /usr/local/bin/vrt-setup-devices.sh

    cat > /etc/systemd/system/vrt-devices.service << 'EOF'
[Unit]
Description=VRT Device Permissions
After=systemd-udev-settle.service
After=systemd-modules-load.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/vrt-setup-devices.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF
    /usr/bin/systemctl daemon-reload || true
    /usr/bin/systemctl enable vrt-devices.service || true

    if /usr/sbin/lsmod | /usr/bin/grep -q "pcie_hotplug"; then
        /usr/sbin/rmmod pcie_hotplug || true
    fi
    /usr/sbin/modprobe pcie_hotplug || true
    /usr/bin/udevadm trigger || true
    /usr/bin/sleep 1
    /usr/local/bin/vrt-setup-devices.sh || true
fi
'''

    preun = r'''
if /usr/sbin/lsmod | /usr/bin/grep -q "pcie_hotplug"; then
    /usr/sbin/rmmod pcie_hotplug || true
fi
'''

    postun = r'''
/usr/bin/rm -f /etc/modules-load.d/amd-vrt.conf || true
if [ -f "/etc/udev/rules.d/99-amd-vrt-permissions.rules" ]; then
    /usr/bin/rm -f /etc/udev/rules.d/99-amd-vrt-permissions.rules
    /usr/bin/udevadm control --reload-rules || true
fi
if [ -f "/etc/systemd/system/vrt-devices.service" ]; then
    /usr/bin/systemctl disable vrt-devices.service || true
    /usr/bin/systemctl stop vrt-devices.service || true
    /usr/bin/rm -f /etc/systemd/system/vrt-devices.service
    /usr/bin/systemctl daemon-reload || true
fi
/usr/bin/rm -f /usr/local/bin/vrt-setup-devices.sh || true
if [ -f "/etc/ld.so.conf.d/amd-vrt.conf" ]; then
    /usr/bin/rm -f /etc/ld.so.conf.d/amd-vrt.conf
    /sbin/ldconfig
fi
'''

    spec = f'''Name:           {PACKAGE_NAME}
Version:        {version}
Release:        {release}%{{?dist}}
Summary:        {summary}
License:        {license_str}
URL:            {url}
BuildArch:      {RPM_ARCH}
{requires}

%description
{summary}

%prep
# Nothing to prep

%build
# Nothing to build (prebuilt binaries)

%install
rm -rf %{{buildroot}}
mkdir -p %{{buildroot}}
# Copy from staging dir into buildroot
cp -a "{stage_dir}/." %{{buildroot}}/

%post
{post}

%preun
{preun}

%postun
{postun}

%files
%defattr(-,root,root,-)
/usr/local/bin/*
/usr/local/lib/*
/usr/local/vrt
/usr/src/pcie-hotplug-drv
/opt/amd/vrt

%changelog
* {datetime.utcnow().strftime("%a %b %d %Y")} AMD <support@amd.com> - {version}-{release}
- Initial build
'''
    with open(spec_path, "w") as f:
        f.write(spec)

def build_rpm(stage_dir, version, repo_root):
    out_dir = os.path.join(repo_root, "deploy", "output")
    os.makedirs(out_dir, exist_ok=True)
    topdirs = rpm_topdirs(out_dir)
    spec_path = os.path.join(topdirs["SPECS"], f"{PACKAGE_NAME}.spec")
    release = "1"

    make_rpm_spec(spec_path, version, release, stage_dir)
    # rpmbuild uses %_topdir to find BUILD, RPMS, etc.
    cmd = f'rpmbuild -bb --define "_topdir {topdirs["TOP"]}" "{spec_path}"'
    run_command(cmd)

    # Find the built RPM in RPMS/<arch>/
    rpm_arch = RPM_ARCH
    arch_dir = os.path.join(topdirs["RPMS"], rpm_arch)
    if not os.path.isdir(arch_dir):
        # some dists put noarch if archless; but we have libs, so expect arch
        arch_dir = os.path.join(topdirs["RPMS"], "noarch")
    rpms = [os.path.join(arch_dir, f) for f in os.listdir(arch_dir) if f.endswith(".rpm")]
    if not rpms:
        print("Failed to find built RPM.")
        sys.exit(1)
    for p in rpms:
        print(f"RPM created: {p}")
    return rpms[0]

# ----------------------- MAIN -----------------------

def main():
    parser = argparse.ArgumentParser(description="Build amd-vrt package as DEB or RPM")
    parser.add_argument("--format", choices=["deb", "rpm", "auto"], default="auto",
                        help="Packaging format (default: auto)")
    args = parser.parse_args()

    repo_root = os.path.abspath(os.getcwd())
    print(f"Repository root directory: {repo_root}")

    version = get_version_from_header(repo_root)
    pkg_format = detect_packaging_format(None if args.format == "auto" else args.format)

    stage_dir = create_stage_tree(repo_root)

    # Build & stage files
    build_and_copy_vrt(repo_root, stage_dir)
    build_and_copy_smi(repo_root, stage_dir)
    copy_pcie_driver(repo_root, stage_dir)
    copy_design_pdi(repo_root, stage_dir)

    # Build packages
    if pkg_format == "deb":
        deb = build_deb(stage_dir, version, repo_root)
        print(f"\nPackage successfully created: {deb}")
        print(f"Install with: sudo apt install ./{os.path.basename(deb)}")
    else:
        rpm = build_rpm(stage_dir, version, repo_root)
        print(f"\nPackage successfully created: {rpm}")
        print(f"Install with: sudo dnf install {rpm}  # or yum")

if __name__ == "__main__":
    main()
